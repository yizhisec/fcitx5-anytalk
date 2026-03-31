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
#include <fcitx-utils/dbus/bus.h>

// Declare the DBusModule function we need
FCITX_ADDON_DECLARE_FUNCTION(DBusModule, bus, fcitx::dbus::Bus *());

// --- AnyTalkDBus Implementation ---

AnyTalkDBus::AnyTalkDBus(AnyTalkEngine *engine) : engine_(engine) {}

void AnyTalkDBus::emitStateChanged(const std::string &state) {
    stateChanged(state);
}

std::string AnyTalkDBus::getState() {
    return engine_->getState();
}

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

std::string AnyTalkEngine::getState() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_state_;
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

  // Initialize D-Bus
  auto *dbusAddon = dbus();
  if (dbusAddon) {
    auto *bus = dbusAddon->call<fcitx::IDBusModule::bus>();
    if (bus) {
      dbusObject_ = std::make_unique<AnyTalkDBus>(this);
      bus->addObjectVTable(Constants::DBUS_PATH, Constants::DBUS_INTERFACE, *dbusObject_);
      bus->requestName(Constants::DBUS_SERVICE, fcitx::dbus::RequestNameFlag{});
    }
  }

  // Register global key event watcher (PreInputMethod phase)
  eventWatcher_ = instance_->watchEvent(
      fcitx::EventType::InputContextKeyEvent,
      fcitx::EventWatcherPhase::PreInputMethod,
      [this](fcitx::Event &event) { handleGlobalKeyEvent(event); });
}

AnyTalkEngine::~AnyTalkEngine() {
  zig_ctx_.reset();
}

void AnyTalkEngine::handleGlobalKeyEvent(fcitx::Event &event) {
  auto &keyEvent = static_cast<fcitx::KeyEvent &>(event);

  if (keyEvent.isRelease()) {
    return;
  }

  bool is_recording;
  std::string state;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state = current_state_;
    is_recording = (state == Constants::STATE_RECORDING);
  }

  // Enter key stops recording (only when recording)
  if (keyEvent.key().sym() == FcitxKey_Return && is_recording) {
      stopAsync();
      setStatus(Constants::STATE_IDLE);
      keyEvent.accept();
      return;
  }

  // F2 or Media Play toggles recording
  if (keyEvent.key().sym() == FcitxKey_F2 || keyEvent.key().sym() == FcitxKey_AudioPlay) {
    if (!is_recording) {
      startAsync();
      if (state != Constants::STATE_CONNECTED) {
          setStatus(Constants::STATE_CONNECTING);
      }
    } else {
      stopAsync();
      setStatus(Constants::STATE_IDLE);
    }
    keyEvent.accept();
    return;
  }
}

void AnyTalkEngine::setStatus(const std::string &state) {
  std::string text_to_commit;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    current_state_ = state;
    if (state == Constants::STATE_IDLE) {
      text_to_commit = std::move(pending_text_);
      pending_text_.clear();
    }
  }

  // Emit D-Bus signal
  if (dbusObject_) {
    dbusObject_->emitStateChanged(state);
  }

  if (!instance_) return;
  auto *ic = instance_->inputContextManager().lastFocusedInputContext();
  if (!ic) return;

  if (state == Constants::STATE_IDLE) {
    if (!text_to_commit.empty()) {
      ic->commitString(text_to_commit);
    }
    ic->inputPanel().setPreedit(fcitx::Text());
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
    // Remove status action when idle
    ic->statusArea().removeAction(statusAction_.get());
  } else if (state == Constants::STATE_RECORDING || state == Constants::STATE_CONNECTING || state == Constants::STATE_CONNECTED) {
    // Add status action when active
    ic->statusArea().addAction(fcitx::StatusGroup::InputMethod, statusAction_.get());
  }
  statusAction_->update(ic);
  ic->updateUserInterface(fcitx::UserInterfaceComponent::StatusArea);
}

void AnyTalkEngine::setConfig(const fcitx::RawConfig &config) {
    config_.load(config, true);
    fcitx::safeSaveAsIni(config_, "conf/anytalk.conf");
}

void AnyTalkEngine::reloadConfig() {
    fcitx::readAsIni(config_, "conf/anytalk.conf");
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

void AnyTalkEngine::updatePreedit(const std::string &text) {
  if (!instance_) return;
  auto *ic = instance_->inputContextManager().lastFocusedInputContext();
  if (!ic) return;
  std::lock_guard<std::mutex> lock(state_mutex_);
  ic->inputPanel().setPreedit(fcitx::Text(pending_text_ + text));
  ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

// Helper function to remove trailing punctuation from text
static std::string removeTrailingPunctuation(std::string text) {
  // Common Chinese and English punctuation marks
  static const char* punctuations[] = {
    "，", "。", "！", "？", "、", "；", "：",  // Chinese
    ",", ".", "!", "?", ";", ":",            // English
    nullptr
  };

  while (!text.empty()) {
    bool removed = false;

    // Check for Chinese punctuation (UTF-8, 3 bytes)
    for (int i = 0; punctuations[i] != nullptr; ++i) {
      const char* p = punctuations[i];
      size_t pLen = strlen(p);
      if (text.size() >= pLen && text.compare(text.size() - pLen, pLen, p) == 0) {
        text.erase(text.size() - pLen);
        removed = true;
        break;
      }
    }

    if (!removed) {
      break;
    }
  }

  return text;
}

void AnyTalkEngine::commitText(const std::string &text) {
  if (!instance_) return;
  auto *ic = instance_->inputContextManager().lastFocusedInputContext();
  if (!ic) return;

  std::lock_guard<std::mutex> lock(state_mutex_);
  std::string processedText = text;
  if (*config_.removeTrailingPunctuation) {
    processedText = removeTrailingPunctuation(text);
  }

  pending_text_ += processedText;
  ic->inputPanel().setPreedit(fcitx::Text(pending_text_));
  ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

class AnyTalkFactory : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
    return new AnyTalkEngine(manager ? manager->instance() : nullptr);
  }
};

FCITX_ADDON_FACTORY(AnyTalkFactory)
