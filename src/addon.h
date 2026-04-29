#pragma once
#include <fcitx/addoninstance.h>
#include <fcitx/instance.h>
#include <fcitx/inputcontext.h>
#include <fcitx/addonmanager.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/dbus/bus.h>
#include <memory>
#include <vector>

namespace fcitx {
class DBusModule;
}

/// Minimal fcitx5 addon — a dumb forwarder for the anytalk-overlay process.
///
/// All recording state lives in the overlay; this addon only does:
///   1. Watch F2 / Esc / Enter globally and forward to the overlay over
///      D-Bus (`ToggleRecording` / `CancelRecording` / `StopRecording`).
///      The overlay decides whether each call is a no-op or an action
///      based on its own state — we never cache it here. Esc and Enter
///      pass through to the focused app too (cancel-dialog / send-line).
///   2. Subscribe to the overlay's `CommitText` signal and translate it into
///      a single `ic->commitString(text)` on the focused input context —
///      the one operation that genuinely requires running inside fcitx5.
///   3. Push WAYLAND_DISPLAY etc. into the session bus at startup so the
///      D-Bus-activated overlay process inherits a usable graphical env.
///
/// No state caching, no status-area icon, no legacy D-Bus surface.
/// Configuration lives entirely in the overlay (`~/.config/fcitx5/conf/anytalk.conf`).
class AnyTalkEngine : public fcitx::AddonInstance {
public:
    AnyTalkEngine(fcitx::Instance *instance);
    ~AnyTalkEngine();

    FCITX_ADDON_DEPENDENCY_LOADER(dbus, instance_->addonManager());

private:
    void handleGlobalKeyEvent(fcitx::Event &event);

    void overlayCall(const char *method);
    void wakeOverlay(fcitx::dbus::Bus *bus);
    void pushDBusEnv(fcitx::dbus::Bus *bus);
    void connectOverlaySignals(fcitx::dbus::Bus *bus);
    void commitText(const std::string &text);

    fcitx::Instance *instance_;
    std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> eventWatcher_;
    fcitx::EventDispatcher dispatcher_;
    std::vector<std::unique_ptr<fcitx::dbus::Slot>> signalSlots_;
};
