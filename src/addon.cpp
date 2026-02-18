#include "addon.h"
#include "constants.h"
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx-utils/log.h>
#include <fcitx/statusarea.h>
#include <fcitx-utils/keysymgen.h>
#include <fcitx/userinterface.h>

// --- AnyTalkStatusAction Implementation ---

AnyTalkStatusAction::AnyTalkStatusAction(AnyTalkEngine *engine) : engine_(engine) {
}

std::string AnyTalkStatusAction::shortText(fcitx::InputContext *ic) const {
    return engine_->getStatusLabel();
}

std::string AnyTalkStatusAction::icon(fcitx::InputContext *ic) const {
    return engine_->getStatusIcon();
}

// --- AnyTalkEngine Implementation ---

// Helper methods for status display
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
    return Constants::ICON_DEFAULT;
}

// Static callback from Zig library - dispatches to main thread
void AnyTalkEngine::zigCallback(void *user_data, AnytalkEventType type, const char *text) {
    auto *engine = static_cast<AnyTalkEngine *>(user_data);
    if (!engine || !engine->instance_) return;

    std::string text_str(text ? text : "");
    engine->dispatcher_.schedule([engine, type, text_str]() {
        switch (type) {
        case ANYTALK_EVENT_PARTIAL:
            engine->updatePreedit(text_str);
            break;
        case ANYTALK_EVENT_FINAL:
            engine->commitText(text_str);
            break;
        case ANYTALK_EVENT_STATUS:
            engine->setStatus(text_str);
            break;
        case ANYTALK_EVENT_ERROR:
            FCITX_ERROR() << "AnyTalk error: " << text_str;
            engine->setStatus(Constants::STATE_IDLE);
            break;
        }
    });
}

AnyTalkEngine::AnyTalkEngine(fcitx::Instance *instance)
  : instance_(instance) {
  dispatcher_.attach(&instance_->eventLoop());

  statusAction_ = std::make_unique<AnyTalkStatusAction>(this);

  reloadConfig();

  // Initialize Zig library
  AnytalkConfig zig_config{};
  std::string appId = *config_.appId;
  std::string accessToken = *config_.accessToken;
  zig_config.app_id = appId.c_str();
  zig_config.access_token = accessToken.c_str();
  zig_config.resource_id = nullptr; // Use default
  zig_config.mode = nullptr;        // Use default

  AnytalkContext *raw_ctx = anytalk_init(&zig_config, zigCallback, this);
  if (!raw_ctx) {
    FCITX_ERROR() << "Failed to initialize AnyTalk Zig library";
  } else {
    zig_ctx_.reset(raw_ctx, [](AnytalkContext *ctx) {
      anytalk_destroy(ctx);
    });
  }
}

AnyTalkEngine::~AnyTalkEngine() {
  zig_ctx_.reset();
}

// Helper to trigger UI refresh
void AnyTalkEngine::setStatus(const std::string &state) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    current_state_ = state;
    if (state == Constants::STATE_IDLE) {
      active_ic_ = nullptr;
    }
  }

  if (instance_) {
     auto *ic = instance_->inputContextManager().lastFocusedInputContext();
     if (ic) {
         statusAction_->update(ic);
         ic->updateUserInterface(fcitx::UserInterfaceComponent::StatusArea);
     }
  }
}

// V2 Overrides for Main Icon/Label
std::string AnyTalkEngine::subModeIconImpl(const fcitx::InputMethodEntry &, fcitx::InputContext &) {
    return getStatusIcon();
}

std::string AnyTalkEngine::subModeLabelImpl(const fcitx::InputMethodEntry &, fcitx::InputContext &) {
    return getStatusLabel();
}

void AnyTalkEngine::setConfig(const fcitx::RawConfig &config) {
    config_.load(config, true);
    fcitx::safeSaveAsIni(config_, "conf/anytalk.conf");
}

void AnyTalkEngine::reloadConfig() {
    fcitx::readAsIni(config_, "conf/anytalk.conf");
}

void AnyTalkEngine::activate(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) {
    auto *ic = event.inputContext();
    if (!ic) return;
    ic->statusArea().addAction(fcitx::StatusGroup::InputMethod, statusAction_.get());

    // Refresh UI on activate
    statusAction_->update(ic);
}

void AnyTalkEngine::deactivate(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (event.inputContext() == active_ic_) {
        active_ic_ = nullptr;
    }
}

void AnyTalkEngine::keyEvent(const fcitx::InputMethodEntry &, fcitx::KeyEvent &event) {
  if (event.isRelease()) {
    return;
  }

  auto *ic = event.inputContext();
  bool is_recording;
  std::string state;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state = current_state_;
    is_recording = (state == Constants::STATE_RECORDING);
  }

  // Enter key stops recording
  if (event.key().sym() == FcitxKey_Return && is_recording) {
      stopAsync();
      setStatus(Constants::STATE_IDLE);
      event.accept();
      return;
  }

  // F2 or Media Play toggles recording
  if (event.key().sym() == FcitxKey_F2 || event.key().sym() == FcitxKey_AudioPlay) {
    if (!is_recording) {
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        active_ic_ = ic;
      }
      startAsync();
      if (state != Constants::STATE_CONNECTED) {
          setStatus(Constants::STATE_CONNECTING);
      }
    } else {
      stopAsync();
      setStatus(Constants::STATE_IDLE);
    }
    event.accept();
    return;
  }
}

void AnyTalkEngine::startAsync() {
  auto ctx = zig_ctx_;
  if (!ctx) return;
  auto flag = start_in_flight_;
  if (flag->exchange(true)) return;
  std::thread([flag, ctx]() {
    anytalk_start(ctx.get());
    flag->store(false);
  }).detach();
}

void AnyTalkEngine::stopAsync() {
  auto ctx = zig_ctx_;
  if (!ctx) return;
  auto flag = stop_in_flight_;
  if (flag->exchange(true)) return;
  std::thread([flag, ctx]() {
    anytalk_stop(ctx.get());
    flag->store(false);
  }).detach();
}

fcitx::InputContext *AnyTalkEngine::resolveIC() {
  if (!instance_) return nullptr;
  auto *focused = instance_->inputContextManager().lastFocusedInputContext();
  std::lock_guard<std::mutex> lock(state_mutex_);
  return active_ic_ ? (active_ic_ == focused ? active_ic_ : nullptr) : focused;
}

void AnyTalkEngine::updatePreedit(const std::string &text) {
  auto *ic = resolveIC();
  if (!ic) return;
  ic->inputPanel().setClientPreedit(fcitx::Text(text));
  ic->updatePreedit();
}

void AnyTalkEngine::commitText(const std::string &text) {
  auto *ic = resolveIC();
  if (!ic) return;
  ic->commitString(text);
  ic->inputPanel().setClientPreedit(fcitx::Text());
  ic->updatePreedit();
}

class AnyTalkFactory : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
    return new AnyTalkEngine(manager ? manager->instance() : nullptr);
  }
};

FCITX_ADDON_FACTORY(AnyTalkFactory)
