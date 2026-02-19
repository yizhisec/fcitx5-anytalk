#pragma once
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <fcitx/inputcontext.h>
#include <fcitx/action.h>
#include <fcitx-config/configuration.h>
#include <fcitx-config/iniparser.h>
#include <mutex>
#include <fcitx-utils/eventdispatcher.h>
#include <atomic>
#include <memory>
#include <thread>

extern "C" {
#include "anytalk_api.h"
}

FCITX_CONFIGURATION(
    AnyTalkConfig,
    fcitx::Option<std::string> appId{this, "AppID", "Volcengine App ID"};
    fcitx::Option<std::string> accessToken{this, "AccessToken", "Volcengine Access Token"};
);

class AnyTalkEngine;

class AnyTalkStatusAction : public fcitx::Action {
public:
    AnyTalkStatusAction(AnyTalkEngine *engine);
    std::string shortText(fcitx::InputContext *ic) const override;
    std::string icon(fcitx::InputContext *ic) const override;
private:
    AnyTalkEngine *engine_;
};

class AnyTalkEngine : public fcitx::InputMethodEngineV2 {
public:
  AnyTalkEngine(fcitx::Instance *instance);
  ~AnyTalkEngine();
  void activate(const fcitx::InputMethodEntry &entry,
                fcitx::InputContextEvent &event) override;
  void deactivate(const fcitx::InputMethodEntry &entry,
                  fcitx::InputContextEvent &event) override;
  void keyEvent(const fcitx::InputMethodEntry &entry,
                fcitx::KeyEvent &event) override;

  void setConfig(const fcitx::RawConfig &config) override;
  void reloadConfig() override;
  const fcitx::Configuration *getConfig() const override { return &config_; }

  void setStatus(const std::string &state);

  // V2 Overrides for Main Icon/Label
  std::string subModeIconImpl(const fcitx::InputMethodEntry &entry, fcitx::InputContext &ic) override;
  std::string subModeLabelImpl(const fcitx::InputMethodEntry &entry, fcitx::InputContext &ic) override;

  // Helper methods for status display
  std::string getStatusLabel() const;
  std::string getStatusIcon() const;

  // Static callback for Zig library
  static void zigCallback(void *user_data, AnytalkEventType type, const char *text);

private:
  void startAsync();
  void stopAsync();
  void updatePreedit(const std::string &text);
  void commitText(const std::string &text);

  fcitx::Instance *instance_;
  fcitx::EventDispatcher dispatcher_;
  std::shared_ptr<AnytalkContext> zig_ctx_;
  AnyTalkConfig config_;
  std::string current_state_{"idle"};
  std::string pending_text_;
  fcitx::InputContext *active_ic_{nullptr};
  std::unique_ptr<AnyTalkStatusAction> statusAction_;
  mutable std::mutex state_mutex_;  // Protects current_state_, pending_text_, active_ic_
  std::shared_ptr<std::atomic_bool> start_in_flight_{std::make_shared<std::atomic_bool>(false)};
  std::shared_ptr<std::atomic_bool> stop_in_flight_{std::make_shared<std::atomic_bool>(false)};
};
