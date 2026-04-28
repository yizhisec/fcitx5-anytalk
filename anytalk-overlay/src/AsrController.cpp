#include "AsrController.h"
#include "Config.h"
#include "OverlayState.h"
#include "asr/AsrBackend.h"
#include "asr/AsrBackendFactory.h"
#include "audio/AudioCapture.h"

#include <QDateTime>
#include <QDebug>

AsrController::AsrController(QObject *parent) : QObject(parent) {}
AsrController::~AsrController() = default;

bool AsrController::applyConfig(const OverlayConfig &cfg) {
    removeTrailingPunctuation_ = cfg.removeTrailingPunctuation;

    backend_ = asr::create(cfg, this);
    if (!backend_) return false;

    connect(backend_.get(), &AsrBackend::partial, this, &AsrController::onBackendPartial);
    connect(backend_.get(), &AsrBackend::final_, this, &AsrController::onBackendFinal);
    connect(backend_.get(), &AsrBackend::error, this, &AsrController::onBackendError);
    connect(backend_.get(), &AsrBackend::connected, this, &AsrController::onBackendConnected);
    connect(backend_.get(), &AsrBackend::finished, this, &AsrController::onBackendFinished);

    if (!audio_) {
        audio_ = std::make_unique<AudioCapture>(this);
        connect(audio_.get(), &AudioCapture::pcm, this, &AsrController::onAudioPcm);
        connect(audio_.get(), &AudioCapture::level, this, &AsrController::onAudioLevel);
        connect(audio_.get(), &AudioCapture::error, this, &AsrController::onAudioError);
        connect(audio_.get(), &AudioCapture::warmedUp, this,
                &AsrController::onAudioWarmedUp);
    }

    // Pre-warm the mic stream so the first F2 doesn't pay for stream creation
    // and PulseAudio's source-suspend silence padding.
    audio_->prewarm();
    return true;
}

QString AsrController::postProcess(const QString &text) const {
    if (!removeTrailingPunctuation_) return text;
    static const QString puncts = QStringLiteral("，。！？、；：,.!?;:");
    QString out = text;
    while (!out.isEmpty() && puncts.contains(out.back())) out.chop(1);
    return out;
}

// ---- Recording lifecycle ----

void AsrController::startRecording() {
    if (!backend_) {
        // Caller should have invoked applyConfig() and got false back; surface
        // for them so the overlay can pop the SettingsDialog.
        emit errorOccurred(QStringLiteral("配置缺失，请先填写 AppID / AccessToken"));
        emit stateChanged(state::Error);
        return;
    }
    if (currentState_ == state::Recording ||
        currentState_ == state::Connecting) {
        return;
    }
    finalBuffer_.clear();
    wsConnected_ = false;
    // mic warm-up is sticky across sessions: once the PA stream has produced
    // real audio, every subsequent F2 hits a hot mic.
    audioWarmedUp_ = audio_ && audio_->isWarmedUp();
    if (!audio_->start()) {
        // AudioCapture already emitted error → onAudioError handled the rest.
        return;
    }
    currentState_ = state::Connecting;
    emit stateChanged(currentState_);
    backend_->start();
}

void AsrController::stopRecording() {
    if (currentState_ != state::Recording &&
        currentState_ != state::Connecting) return;
    if (audio_) audio_->stop();
    if (backend_) backend_->stop();
    // Don't enterIdle yet — the backend still needs to drain remaining
    // server-side finals after our LAST audio frame. enterIdle runs in
    // onBackendFinished, which fires after the WebSocket cleanly closes.
}

void AsrController::toggleRecording() {
    if (currentState_ == state::Recording ||
        currentState_ == state::Connecting) {
        stopRecording();
    } else {
        startRecording();
    }
}

void AsrController::cancelRecording() {
    if (audio_) audio_->stop();
    if (backend_) backend_->cancel();
    // Cancel discards: drop accumulated text, no commit. Going straight to
    // idle is correct here because we don't expect any further finals.
    finalBuffer_.clear();
    enterIdle(/*fromError=*/false);
}

void AsrController::enterIdle(bool fromError) {
    currentState_ = state::Idle;
    if (!fromError && !finalBuffer_.isEmpty()) {
        emit commitText(finalBuffer_);
    }
    finalBuffer_.clear();
    emit stateChanged(currentState_);
}

// ---- Audio events ----

void AsrController::onAudioPcm(const QByteArray &chunk) {
    if (backend_ && currentState_ != state::Idle &&
        currentState_ != state::Error) {
        backend_->pushPcm(chunk);
    }
}

void AsrController::onAudioLevel(double level) {
    // Throttle to ~20 Hz. AudioCapture fires every 40 ms (~25 Hz); without
    // this gate every level value re-broadcasts on D-Bus, which is wasteful
    // when external observers (waybar) are on the bus.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastLevelEmitMs_ < 50) return;
    lastLevelEmitMs_ = now;
    emit audioLevel(level);
}

void AsrController::onAudioError(const QString &msg) {
    finalBuffer_.clear();
    if (backend_) backend_->cancel();
    emit errorOccurred(msg);
    currentState_ = state::Error;
    emit stateChanged(currentState_);
}

// ---- Backend events ----

void AsrController::onBackendConnected() {
    wsConnected_ = true;
    maybeEnterRecording();
}

void AsrController::onAudioWarmedUp() {
    audioWarmedUp_ = true;
    maybeEnterRecording();
}

void AsrController::maybeEnterRecording() {
    if (currentState_ != state::Connecting) return;
    if (!wsConnected_ || !audioWarmedUp_) return;
    currentState_ = state::Recording;
    emit stateChanged(currentState_);
}

void AsrController::onBackendPartial(const QString &text) {
    emit transcriptPartial(text);
}

void AsrController::onBackendFinal(const QString &text) {
    const QString processed = postProcess(text);
    finalBuffer_ += processed;
    emit transcriptFinal(processed);
}

void AsrController::onBackendError(const QString &msg) {
    finalBuffer_.clear();
    if (audio_) audio_->stop();
    emit errorOccurred(msg);
    currentState_ = state::Error;
    emit stateChanged(currentState_);
}

void AsrController::onBackendFinished() {
    if (currentState_ == state::Idle ||
        currentState_ == state::Error) return;
    if (audio_) audio_->stop();
    enterIdle(/*fromError=*/false);
}
