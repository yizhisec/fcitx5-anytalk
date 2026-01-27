#include "addon.h"
#include "constants.h"
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx-utils/log.h>
#include <fcitx/statusarea.h>
#include <fcitx-utils/keysymgen.h>
#include <regex>
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
    if (recording_) return Constants::LABEL_RECORDING;
    if (current_state_ == Constants::STATE_CONNECTING) return Constants::LABEL_CONNECTING;
    if (current_state_ == Constants::STATE_CONNECTED) return Constants::LABEL_READY;
    return Constants::LABEL_DEFAULT;
}

std::string AnyTalkEngine::getStatusIcon() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (recording_) return Constants::ICON_RECORDING;
    return Constants::ICON_DEFAULT;
}

AnyTalkEngine::AnyTalkEngine(fcitx::Instance *instance)
  : instance_(instance) {
  ipc_.setCallbacks(
    [this](const std::string &text) {
      if (!instance_) return;
      instance_->eventDispatcher().schedule([this, text]() {
        updatePreedit(text);
      });
    },
    [this](const std::string &text) {
      if (!instance_) return;
      instance_->eventDispatcher().schedule([this, text]() {
        commitText(text);
      });
    },
    [this](const std::string &state) {
      if (!instance_) return;
      instance_->eventDispatcher().schedule([this, state]() {
        setStatus(state);
      });
    }
  );
  ipc_.start();

  statusAction_ = std::make_unique<AnyTalkStatusAction>(this);
  
  reloadConfig();
  startDaemon();
}

AnyTalkEngine::~AnyTalkEngine() {
  ipc_.stop();
}

// Helper to trigger UI refresh
void AnyTalkEngine::setStatus(const std::string &state) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    current_state_ = state;
    if (state == Constants::STATE_IDLE) {
      recording_ = false;
      active_ic_ = nullptr;
    } else if (state == Constants::STATE_CONNECTED) {
      recording_ = false;
    } else if (state == Constants::STATE_RECORDING) {
      recording_ = true;
    }
  }

  if (instance_) {
     auto *ic = instance_->inputContextManager().lastFocusedInputContext();
     if (ic) {
         statusAction_->update(ic);
         ic->updateUserInterface(fcitx::UserInterfaceComponent::StatusArea);
         // Also update input panel if needed, but StatusArea covers icons/labels
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

void AnyTalkEngine::startDaemon() {
    std::string daemonPath = *config_.daemonPath;
    if (daemonPath.empty()) {
        daemonPath = "anytalk-daemon";
    }

    daemon_manager_.start(
        daemonPath,
        *config_.appId,
        *config_.accessToken,
        *config_.developerMode
    );
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

  // Enter key
  bool is_recording = false;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    is_recording = recording_;
  }

  if (event.key().sym() == FcitxKey_Return && is_recording) {
      FCITX_DEBUG() << "Enter pressed, stopping recording";
      ipc_.sendStop();

      // Update UI immediately
      setStatus(Constants::STATE_IDLE); // This triggers refresh and updates recording_ safely
      event.accept();
      return;
  }

  // F2 or Media Play key
  if (event.key().sym() == FcitxKey_F2 || event.key().sym() == FcitxKey_AudioPlay) {
    bool should_start = false;
    std::string state;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      should_start = !recording_;
      state = current_state_;
      if (should_start) {
        active_ic_ = ic;
      }
    }

    if (should_start) {
      ignore_next_commit_ = false;
      ipc_.sendStart();
      if (state != Constants::STATE_CONNECTED) {
          setStatus(Constants::STATE_CONNECTING);
      }
    } else {
      ipc_.sendStop();
      setStatus(Constants::STATE_IDLE); // Optimistic update
    }
    event.accept();
    return;
  }
}

void AnyTalkEngine::updatePreedit(const std::string &text) {
  if (!instance_) return;

  last_text_ = text;
  auto *focused = instance_->inputContextManager().lastFocusedInputContext();

  fcitx::InputContext *ic = nullptr;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    ic = active_ic_ ? (active_ic_ == focused ? active_ic_ : nullptr) : focused;
  }

  if (!ic) return;
  fcitx::Text preedit(text);
  ic->inputPanel().setClientPreedit(preedit);
  ic->updatePreedit();
}

void AnyTalkEngine::commitText(const std::string &text) {
  if (ignore_next_commit_) return;
  if (!instance_) return;
  last_text_ = "";
  auto *focused = instance_->inputContextManager().lastFocusedInputContext();

  fcitx::InputContext *ic = nullptr;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    ic = active_ic_ ? (active_ic_ == focused ? active_ic_ : nullptr) : focused;
  }

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
