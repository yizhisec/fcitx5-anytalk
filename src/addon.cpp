#include "addon.h"
#include "constants.h"

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
            pushDBusEnv(bus);
            connectOverlaySignals(bus);
            wakeOverlay(bus);
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
    // are swallowed (unambiguously ours); Esc and Enter are forwarded but
    // also passed through to the focused application so the user's natural
    // "cancel and close dialog" / "commit transcript and send the line"
    // expectations both work. Safe to forward Enter again now that
    // overlayCall() gates on bus->serviceOwner — no more accidental
    // auto-activation when overlay is dead (pre-pkill freeze trigger).
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
    // Skip if overlay is not running. We deliberately do NOT let D-Bus
    // auto-activation re-spawn it here: after `pkill`, the BT SCO link
    // may be in a half-released kernel state (CVE-2025-40309 family),
    // and a fresh overlay racing on the same device tends to wedge the
    // seat. wakeOverlay() at addon construct is the only spawn path.
    if (bus->serviceOwner(kOverlayService, /*usec=*/100000).empty()) return;
    auto msg = bus->createMethodCall(kOverlayService, kOverlayPath,
                                      kOverlayInterface, method);
    msg.send();
}

void AnyTalkEngine::wakeOverlay(fcitx::dbus::Bus *bus) {
    // Trigger D-Bus session-bus auto-activation so the overlay is alive
    // before the user touches F2. We deliberately bypass overlayHasOwner()
    // — that gate prevents "F2 right after pkill" from re-spawning, but
    // at addon load (fresh fcitx5 boot OR `fcitx5 -r`) we *want* to spawn.
    // Fire-and-forget Ping; if the overlay is already up the call is a
    // no-op, and if it's down the daemon will spawn it via the .service
    // file.
    if (!bus) return;
    auto msg = bus->createMethodCall(kOverlayService, kOverlayPath,
                                      kOverlayInterface, "Ping");
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
