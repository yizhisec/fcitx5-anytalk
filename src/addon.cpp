#include "addon.h"
#include "constants.h"

#include <cstdlib>
#include <unordered_map>

#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterface.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/dbus/message.h>

FCITX_ADDON_DECLARE_FUNCTION(DBusModule, bus, fcitx::dbus::Bus *());

namespace {
constexpr const char *kOverlayService = "org.fcitx.Fcitx5.AnyTalk.Overlay";
constexpr const char *kOverlayPath = "/overlay";
constexpr const char *kOverlayInterface = "org.fcitx.Fcitx5.AnyTalk.Overlay";
} // namespace

// ---------------- AnyTalkDBus ----------------

AnyTalkDBus::AnyTalkDBus(AnyTalkEngine *engine) : engine_(engine) {}

void AnyTalkDBus::emitStateChanged(const std::string &state) {
    stateChanged(state);
}

std::string AnyTalkDBus::getState() { return engine_->getState(); }

// ---------------- AnyTalkStatusAction ----------------

AnyTalkStatusAction::AnyTalkStatusAction(AnyTalkEngine *engine) : engine_(engine) {}

std::string AnyTalkStatusAction::shortText(fcitx::InputContext *) const {
    return engine_->getStatusLabel();
}

std::string AnyTalkStatusAction::icon(fcitx::InputContext *) const {
    return engine_->getStatusIcon();
}

// ---------------- AnyTalkEngine ----------------

std::string AnyTalkEngine::getStatusLabel() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (current_state_ == Constants::STATE_RECORDING) return Constants::LABEL_RECORDING;
    if (current_state_ == Constants::STATE_CONNECTING) return Constants::LABEL_CONNECTING;
    if (current_state_ == Constants::STATE_CONNECTED) return Constants::LABEL_READY;
    return Constants::LABEL_DEFAULT;
}

std::string AnyTalkEngine::getStatusIcon() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (current_state_ == Constants::STATE_RECORDING) return Constants::ICON_RECORDING;
    if (current_state_ == Constants::STATE_CONNECTING) return Constants::ICON_CONNECTING;
    if (current_state_ == Constants::STATE_CONNECTED) return Constants::ICON_READY;
    return Constants::ICON_IDLE;
}

std::string AnyTalkEngine::getState() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_state_;
}

AnyTalkEngine::AnyTalkEngine(fcitx::Instance *instance) : instance_(instance) {
    dispatcher_.attach(&instance_->eventLoop());

    statusAction_ = std::make_unique<AnyTalkStatusAction>(this);

    reloadConfig();

    // D-Bus setup: register our service for legacy observers, push session
    // env into dbus daemon (so the overlay activated process gets WAYLAND_DISPLAY),
    // and subscribe to the overlay's signal stream.
    auto *dbusAddon = dbus();
    if (dbusAddon) {
        auto *bus = dbusAddon->call<fcitx::IDBusModule::bus>();
        if (bus) {
            dbusObject_ = std::make_unique<AnyTalkDBus>(this);
            bus->addObjectVTable(Constants::DBUS_PATH, Constants::DBUS_INTERFACE, *dbusObject_);
            bus->requestName(Constants::DBUS_SERVICE, fcitx::dbus::RequestNameFlag{});
            pushDBusEnv(bus);
            connectOverlaySignals(bus);
        }
    }

    eventWatcher_ = instance_->watchEvent(
        fcitx::EventType::InputContextKeyEvent,
        fcitx::EventWatcherPhase::PreInputMethod,
        [this](fcitx::Event &event) { handleGlobalKeyEvent(event); });
}

AnyTalkEngine::~AnyTalkEngine() = default;

void AnyTalkEngine::setConfig(const fcitx::RawConfig &config) {
    config_.load(config, true);
    fcitx::safeSaveAsIni(config_, "conf/anytalk.conf");
    // Note: overlay re-reads this file only at process start. Restarting the
    // overlay (`pkill -x anytalk-overlay`) picks up the new credentials.
}

void AnyTalkEngine::reloadConfig() {
    fcitx::readAsIni(config_, "conf/anytalk.conf");
}

void AnyTalkEngine::handleGlobalKeyEvent(fcitx::Event &event) {
    auto &keyEvent = static_cast<fcitx::KeyEvent &>(event);
    if (keyEvent.isRelease()) return;

    std::string state;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state = current_state_;
    }
    const bool is_recording = (state == Constants::STATE_RECORDING);
    const bool is_error = (state == Constants::STATE_ERROR);

    // Enter while recording → commit (overlay does Stop, the eventual final
    // segment will be committed when TranscriptFinal arrives).
    if (keyEvent.key().sym() == FcitxKey_Return && is_recording) {
        overlayCall("StopRecording");
        keyEvent.accept();
        return;
    }

    // Escape during recording = cancel; during the error toast = dismiss.
    // We can't rely on the overlay window receiving focus on Wayland
    // (layer-shell with OnDemand keyboard interactivity), so the addon's
    // global watcher handles it.
    if (keyEvent.key().sym() == FcitxKey_Escape) {
        if (is_recording) {
            overlayCall("CancelRecording");
            applyState(Constants::STATE_IDLE);
            keyEvent.accept();
            return;
        }
        if (is_error) {
            overlayCall("Hide");
            applyState(Constants::STATE_IDLE);
            keyEvent.accept();
            return;
        }
        // Not our state → let other handlers see Esc.
    }

    // F2 / Media Play → toggle, with a special case for the error toast:
    // pressing F2 while an error is showing dismisses the overlay rather than
    // immediately retrying — matches the "再次 F2 应该关闭" expectation.
    if (keyEvent.key().sym() == FcitxKey_F2 || keyEvent.key().sym() == FcitxKey_AudioPlay) {
        if (is_recording) {
            overlayCall("StopRecording");
        } else if (is_error) {
            overlayCall("Hide");
            applyState(Constants::STATE_IDLE);
        } else {
            overlayCall("StartRecording");
        }
        keyEvent.accept();
        return;
    }
}

void AnyTalkEngine::overlayCall(const char *method) {
    auto *dbusAddon = dbus();
    if (!dbusAddon) return;
    auto *bus = dbusAddon->call<fcitx::IDBusModule::bus>();
    if (!bus) return;
    auto msg = bus->createMethodCall(kOverlayService, kOverlayPath,
                                      kOverlayInterface, method);
    msg.send(); // fire-and-forget; activation will spawn overlay if needed
}

void AnyTalkEngine::pushDBusEnv(fcitx::dbus::Bus *bus) {
    if (!bus) return;
    static const char *kVars[] = {
        "WAYLAND_DISPLAY", "DISPLAY",
        "XDG_SESSION_TYPE", "XDG_CURRENT_DESKTOP", "XDG_RUNTIME_DIR",
    };
    std::unordered_map<std::string, std::string> env;
    for (const char *var : kVars) {
        if (const char *val = std::getenv(var); val && *val) env.emplace(var, val);
    }
    if (env.empty()) return;
    auto msg = bus->createMethodCall(
        "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "UpdateActivationEnvironment");
    using fcitx::dbus::Container;
    using fcitx::dbus::ContainerEnd;
    using fcitx::dbus::Signature;
    msg << Container(Container::Type::Array, Signature("{ss}"));
    for (const auto &[k, v] : env) {
        msg << Container(Container::Type::DictEntry, Signature("ss"));
        msg << k << v;
        msg << ContainerEnd{};
    }
    msg << ContainerEnd{};
    msg.send();
}

void AnyTalkEngine::connectOverlaySignals(fcitx::dbus::Bus *bus) {
    auto subscribe = [&](const char *signalName, auto handler) {
        fcitx::dbus::MatchRule rule(
            /*service=*/kOverlayService,
            /*path=*/kOverlayPath,
            /*interface=*/kOverlayInterface,
            /*name=*/signalName);
        auto slot = bus->addMatch(
            rule,
            [handler](fcitx::dbus::Message &msg) {
                std::string s;
                msg >> s;
                handler(s);
                return true;
            });
        if (slot) signalSlots_.push_back(std::move(slot));
    };

    subscribe("StateChanged",
              [this](const std::string &s) { onOverlayStateChanged(s); });
    subscribe("CommitText",
              [this](const std::string &s) { onOverlayCommitText(s); });
    subscribe("ErrorOccurred",
              [this](const std::string &s) { onOverlayErrorOccurred(s); });
    // Note: TranscriptPartial / TranscriptFinal are not subscribed by the
    // addon any more — the overlay shows transcript itself, and only the
    // single CommitText signal at session end drives ic->commitString().
}

void AnyTalkEngine::onOverlayStateChanged(const std::string &state) {
    dispatcher_.schedule([this, state]() { applyState(state); });
}

void AnyTalkEngine::onOverlayCommitText(const std::string &text) {
    dispatcher_.schedule([this, text]() { commitText(text); });
}

void AnyTalkEngine::onOverlayErrorOccurred(const std::string &text) {
    FCITX_ERROR() << "Overlay error: " << text;
    // State transitions are driven by overlay's StateChanged stream; the toast
    // is purely visual. We only log here.
}

void AnyTalkEngine::applyState(const std::string &state) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_ = state;
    }
    if (dbusObject_) dbusObject_->emitStateChanged(state);

    auto *ic = instance_->inputContextManager().lastFocusedInputContext();
    if (!ic) return;

    if (state == Constants::STATE_IDLE) {
        ic->statusArea().removeAction(statusAction_.get());
    } else {
        ic->statusArea().addAction(fcitx::StatusGroup::InputMethod, statusAction_.get());
    }
    statusAction_->update(ic);
    ic->updateUserInterface(fcitx::UserInterfaceComponent::StatusArea);
}

void AnyTalkEngine::commitText(const std::string &text) {
    if (text.empty()) return;
    auto *ic = instance_->inputContextManager().lastFocusedInputContext();
    if (!ic) return;
    ic->commitString(text);
}

class AnyTalkFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new AnyTalkEngine(manager ? manager->instance() : nullptr);
    }
};

FCITX_ADDON_FACTORY(AnyTalkFactory)
