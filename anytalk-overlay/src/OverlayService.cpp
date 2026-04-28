#include "OverlayService.h"
#include "AsrController.h"
#include "OverlayWindow.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>

namespace {
constexpr const char *kService = "org.fcitx.Fcitx5.AnyTalk.Overlay";
constexpr const char *kPath = "/overlay";
} // namespace

OverlayService::OverlayService(OverlayWindow *window, AsrController *asr, QObject *parent)
    : QObject(parent), window_(window), asr_(asr) {}

bool OverlayService::registerOnBus() {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qWarning() << "OverlayService: cannot connect to session bus";
        return false;
    }
    if (!bus.registerObject(kPath, this,
                            QDBusConnection::ExportScriptableSlots |
                            QDBusConnection::ExportScriptableSignals)) {
        qWarning() << "OverlayService: registerObject failed";
        return false;
    }
    if (!bus.registerService(kService)) {
        qWarning() << "OverlayService: registerService failed —"
                   << bus.lastError().message();
        return false;
    }
    return true;
}

void OverlayService::Show() {
    if (window_) window_->onStateChanged(QStringLiteral("recording"));
}

void OverlayService::Hide() {
    if (window_) window_->onStateChanged(QStringLiteral("idle"));
}

void OverlayService::Ping() {}

void OverlayService::StartRecording() {
    if (asr_) asr_->startRecording();
}

void OverlayService::StopRecording() {
    if (asr_) asr_->stopRecording();
}

void OverlayService::CancelRecording() {
    if (asr_) asr_->cancelRecording();
}

void OverlayService::OpenSettings() { emit openSettingsRequested(); }
