#pragma once
#include <fcitx/addoninstance.h>
#include <fcitx/instance.h>
#include <fcitx/inputcontext.h>
#include <fcitx/action.h>
#include <fcitx-config/configuration.h>
#include <fcitx-config/iniparser.h>
#include <fcitx/addonmanager.h>
#include <mutex>
#include <fcitx-utils/eventdispatcher.h>
#include <atomic>
#include <memory>
#include <thread>
#include <fcitx-utils/dbus/objectvtable.h>

extern "C" {
#include "anytalk_api.h"
}

namespace fcitx {
class DBusModule;
}

FCITX_CONFIGURATION(
    AnyTalkConfig,
    fcitx::Option<std::string> appId{this, "AppID", "Volcengine App ID"};
    fcitx::Option<std::string> accessToken{this, "AccessToken", "Volcengine Access Token"};
    fcitx::Option<bool> removeTrailingPunctuation{this, "RemoveTrailingPunctuation", "Remove Trailing Punctuation", false};
);

class AnyTalkEngine;

class AnyTalkDBus : public fcitx::dbus::ObjectVTable<AnyTalkDBus> {
public:
    AnyTalkDBus(AnyTalkEngine *engine);

    // Signal emitter
    void emitStateChanged(const std::string &state);

    // Property getter
    std::string getState();

private:
    AnyTalkEngine *engine_;

    FCITX_OBJECT_VTABLE_PROPERTY(
        state, "State", "s",
        [this]() { return getState(); });
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

  void setConfig(const fcitx::RawConfig &config) override;
  void reloadConfig() override;
  const fcitx::Configuration *getConfig() const override { return &config_; }

  void setStatus(const std::string &state);

  // Helper methods for status display
  std::string getStatusLabel() const;
  std::string getStatusIcon() const;
  std::string getState() const;

  // Static callback for Zig library
  static void zigCallback(void *user_data, AnytalkEventType type, const char *text);

  FCITX_ADDON_DEPENDENCY_LOADER(dbus, instance_->addonManager());

private:
  void handleGlobalKeyEvent(fcitx::Event &event);
  void startAsync();
  void stopAsync();
  void updatePreedit(const std::string &text);
  void commitText(const std::string &text);

  fcitx::Instance *instance_;
  std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> eventWatcher_;
  fcitx::EventDispatcher dispatcher_;
  std::shared_ptr<AnytalkContext> zig_ctx_;
  AnyTalkConfig config_;
  std::string current_state_{"idle"};
  std::string pending_text_;
  std::unique_ptr<AnyTalkStatusAction> statusAction_;
  std::unique_ptr<AnyTalkDBus> dbusObject_;
  mutable std::mutex state_mutex_;  // Protects current_state_, pending_text_
  std::shared_ptr<std::atomic_bool> start_in_flight_{std::make_shared<std::atomic_bool>(false)};
  std::shared_ptr<std::atomic_bool> stop_in_flight_{std::make_shared<std::atomic_bool>(false)};
};
