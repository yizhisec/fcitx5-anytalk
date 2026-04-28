#pragma once
#include <fcitx/addoninstance.h>
#include <fcitx/instance.h>
#include <fcitx/inputcontext.h>
#include <fcitx/action.h>
#include <fcitx/addonmanager.h>
#include <fcitx-config/configuration.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/dbus/objectvtable.h>
#include <fcitx-utils/dbus/bus.h>
#include <memory>
#include <mutex>
#include <string>

FCITX_CONFIGURATION(
    AnyTalkConfig,
    fcitx::Option<std::string> appId{this, "AppID", "Volcengine App ID"};
    fcitx::Option<std::string> accessToken{this, "AccessToken", "Volcengine Access Token"};
    fcitx::Option<bool> removeTrailingPunctuation{this, "RemoveTrailingPunctuation", "Remove Trailing Punctuation", false};
);

namespace fcitx {
class DBusModule;
}

class AnyTalkEngine;

/// Thin D-Bus surface kept for legacy observers (e.g. waybar custom modules
/// that subscribed to StateChanged before the overlay-centric refactor).
/// We mirror the overlay's StateChanged into our own bus name.
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

class AnyTalkEngine : public fcitx::AddonInstance {
public:
    AnyTalkEngine(fcitx::Instance *instance);
    ~AnyTalkEngine();

    // Configuration (kept so fcitx5-config-qt can still edit AppID/Token).
    // The overlay process reads the same INI file at startup.
    void setConfig(const fcitx::RawConfig &config) override;
    void reloadConfig() override;
    const fcitx::Configuration *getConfig() const override { return &config_; }

    // Status helpers (used by AnyTalkStatusAction)
    std::string getStatusLabel() const;
    std::string getStatusIcon() const;
    std::string getState() const;

    FCITX_ADDON_DEPENDENCY_LOADER(dbus, instance_->addonManager());

private:
    void handleGlobalKeyEvent(fcitx::Event &event);

    // D-Bus calls into the overlay process.
    void overlayCall(const char *method);
    void pushDBusEnv(fcitx::dbus::Bus *bus);

    // D-Bus signal handlers (from overlay).
    void connectOverlaySignals(fcitx::dbus::Bus *bus);
    void onOverlayStateChanged(const std::string &state);
    void onOverlayCommitText(const std::string &text);
    void onOverlayErrorOccurred(const std::string &text);

    // UI mutators (always invoked on the fcitx5 main thread via dispatcher_).
    void applyState(const std::string &state);
    void commitText(const std::string &text);

    fcitx::Instance *instance_;
    std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> eventWatcher_;
    fcitx::EventDispatcher dispatcher_;
    std::unique_ptr<AnyTalkStatusAction> statusAction_;
    std::unique_ptr<AnyTalkDBus> dbusObject_;
    std::vector<std::unique_ptr<fcitx::dbus::Slot>> signalSlots_;
    AnyTalkConfig config_;

    mutable std::mutex state_mutex_;
    std::string current_state_{"idle"};
};
