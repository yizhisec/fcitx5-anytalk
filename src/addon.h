#pragma once
#include <fcitx/addoninstance.h>
#include <fcitx/instance.h>
#include <fcitx/inputcontext.h>
#include <fcitx/action.h>
#include <fcitx/addonmanager.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/dbus/objectvtable.h>
#include <fcitx-utils/dbus/bus.h>
#include <memory>
#include <mutex>
#include <string>

namespace fcitx {
class DBusModule;
}

class AnyTalkEngine;

/// Thin D-Bus surface for legacy observers (e.g. waybar custom modules).
/// Mirrors the overlay's StateChanged into our own well-known name.
class AnyTalkDBus : public fcitx::dbus::ObjectVTable<AnyTalkDBus> {
public:
    AnyTalkDBus(AnyTalkEngine *engine);
    void emitStateChanged(const std::string &state);
    std::string getState();

private:
    AnyTalkEngine *engine_;
    FCITX_OBJECT_VTABLE_PROPERTY(state, "State", "s", [this]() { return getState(); });
    FCITX_OBJECT_VTABLE_SIGNAL(stateChanged, "StateChanged", "s");
};

class AnyTalkStatusAction : public fcitx::Action {
public:
    AnyTalkStatusAction(AnyTalkEngine *engine);
    std::string shortText(fcitx::InputContext *ic) const override;
    std::string icon(fcitx::InputContext *ic) const override;
private:
    AnyTalkEngine *engine_;
};

/// Minimal fcitx5 addon. Only responsible for IM-side integration:
///   1. Globally watch F2 / Esc and route to overlay over D-Bus.
///   2. Subscribe to overlay signals; commit the final transcript when it
///      arrives; mirror state to legacy observers.
///   3. Push WAYLAND_DISPLAY etc. into the session bus so the activated
///      overlay process gets a working graphical environment.
///
/// Configuration lives entirely in the overlay (anytalk.conf + a settings
/// dialog). This addon does not read or write the file.
class AnyTalkEngine : public fcitx::AddonInstance {
public:
    AnyTalkEngine(fcitx::Instance *instance);
    ~AnyTalkEngine();

    std::string getStatusLabel() const;
    std::string getStatusIcon() const;
    std::string getState() const;

    FCITX_ADDON_DEPENDENCY_LOADER(dbus, instance_->addonManager());

private:
    void handleGlobalKeyEvent(fcitx::Event &event);

    void overlayCall(const char *method);
    void pushDBusEnv(fcitx::dbus::Bus *bus);

    void connectOverlaySignals(fcitx::dbus::Bus *bus);
    void onOverlayStateChanged(const std::string &state);
    void onOverlayCommitText(const std::string &text);
    void onOverlayErrorOccurred(const std::string &text);

    void applyState(const std::string &state);
    void commitText(const std::string &text);

    fcitx::Instance *instance_;
    std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> eventWatcher_;
    fcitx::EventDispatcher dispatcher_;
    std::unique_ptr<AnyTalkStatusAction> statusAction_;
    std::unique_ptr<AnyTalkDBus> dbusObject_;
    std::vector<std::unique_ptr<fcitx::dbus::Slot>> signalSlots_;

    mutable std::mutex state_mutex_;
    std::string current_state_{"idle"};
};
