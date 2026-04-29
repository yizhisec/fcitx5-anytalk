#include "addon.h"

#include <cstdlib>
#include <unordered_map>

#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
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

AnyTalkEngine::AnyTalkEngine(fcitx::Instance *instance) : instance_(instance) {
    dispatcher_.attach(&instance_->eventLoop());

    auto *dbusAddon = dbus();
    if (dbusAddon) {
        if (auto *bus = dbusAddon->call<fcitx::IDBusModule::bus>()) {
            // Push graphical-env vars so the D-Bus-activated overlay
            // process inherits WAYLAND_DISPLAY etc. on Sway / wlroots.
            pushDBusEnv(bus);
            // Subscribe before any overlay is alive — D-Bus auto-activation
            // can spawn one between F2 and our match-rule registration.
            connectOverlaySignals(bus);
        }
    }

    eventWatcher_ = instance_->watchEvent(
        fcitx::EventType::InputContextKeyEvent,
        fcitx::EventWatcherPhase::PreInputMethod,
        [this](fcitx::Event &event) { handleGlobalKeyEvent(event); });
}

AnyTalkEngine::~AnyTalkEngine() = default;

void AnyTalkEngine::handleGlobalKeyEvent(fcitx::Event &event) {
    auto &keyEvent = static_cast<fcitx::KeyEvent &>(event);
    if (keyEvent.isRelease()) return;

    // Dumb forward — the overlay is the state owner and decides whether each
    // method is a no-op (idle) or an action (active session). F2/AudioPlay
    // are swallowed (unambiguously ours); Esc and Enter pass through to the
    // focused application so the user's natural "cancel and close dialog" /
    // "commit transcript and send the line" expectations both work.
    const auto sym = keyEvent.key().sym();
    if (sym == FcitxKey_F2 || sym == FcitxKey_AudioPlay) {
        overlayCall("ToggleRecording");
        keyEvent.accept();
        return;
    }
    if (sym == FcitxKey_Return) {
        overlayCall("StopRecording");
        return;
    }
    if (sym == FcitxKey_Escape) {
        overlayCall("CancelRecording");
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
    msg.send();
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
    // Only signal we care about: CommitText. The transcript ends up in the
    // focused IC via the one operation only fcitx5 can do — ic->commitString.
    fcitx::dbus::MatchRule rule(
        /*service=*/kOverlayService,
        /*path=*/kOverlayPath,
        /*interface=*/kOverlayInterface,
        /*name=*/"CommitText");
    auto slot = bus->addMatch(rule, [this](fcitx::dbus::Message &msg) {
        std::string text;
        msg >> text;
        dispatcher_.schedule([this, text]() { commitText(text); });
        return true;
    });
    if (slot) signalSlots_.push_back(std::move(slot));
}

void AnyTalkEngine::commitText(const std::string &text) {
    // Always Acknowledge — empty text or no focused IC still need to
    // release the overlay's exit gate, otherwise it spins on the 5 s
    // ackTimer.
    if (!text.empty()) {
        if (auto *ic = instance_->inputContextManager().lastFocusedInputContext()) {
            ic->commitString(text);
        }
    }
    overlayCall("Acknowledge");
}

class AnyTalkFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new AnyTalkEngine(manager ? manager->instance() : nullptr);
    }
};

FCITX_ADDON_FACTORY(AnyTalkFactory)
