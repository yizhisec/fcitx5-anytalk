#include "AsrController.h"

#include <QDebug>
#include <QMetaObject>
#include <QTimer>
#include <QtConcurrent>
#include <QThread>
#include <QtGlobal>

AsrController::AsrController(QObject *parent) : QObject(parent) {}

AsrController::~AsrController() {
    if (ctx_) {
        anytalk_destroy(ctx_);
        ctx_ = nullptr;
    }
}

bool AsrController::initialise(const OverlayConfig &cfg) {
    if (cfg.appId.isEmpty() || cfg.accessToken.isEmpty()) {
        qWarning() << "AsrController: missing AppID/AccessToken in"
                      " ~/.config/fcitx5/conf/anytalk.conf";
        return false;
    }
    removeTrailingPunctuation_ = cfg.removeTrailingPunctuation;

    const QByteArray appId = cfg.appId.toUtf8();
    const QByteArray accessToken = cfg.accessToken.toUtf8();
    AnytalkConfig zig_config{};
    zig_config.app_id = appId.constData();
    zig_config.access_token = accessToken.constData();
    zig_config.resource_id = nullptr;
    zig_config.mode = nullptr;

    ctx_ = anytalk_init(&zig_config, &AsrController::zigCallback, this);
    if (!ctx_) {
        qWarning() << "AsrController: anytalk_init failed";
        return false;
    }
    return true;
}

void AsrController::zigCallback(void *user_data, AnytalkEventType type, const char *text) {
    auto *self = static_cast<AsrController *>(user_data);
    if (!self) return;
    const QString s = QString::fromUtf8(text ? text : "");

    // Hop to the main thread so signals are emitted on a Qt-managed thread.
    QMetaObject::invokeMethod(self, [self, type, s]() {
        switch (type) {
        case ANYTALK_EVENT_PARTIAL:
            emit self->transcriptPartial(s);
            break;
        case ANYTALK_EVENT_FINAL: {
            const QString processed = self->postProcess(s);
            self->finalBuffer_ += processed;
            emit self->transcriptFinal(processed);
            break;
        }
        case ANYTALK_EVENT_STATUS:
            emit self->stateChanged(s);
            if (s == QStringLiteral("recording")) {
                // New session — start fresh.
                self->finalBuffer_.clear();
            } else if (s == QStringLiteral("idle")) {
                // Session ended naturally. Deliver the accumulated text in one
                // shot. Post-processing hooks (LLM polish etc.) plug in here in
                // the future, between accumulation and the commitText signal.
                if (!self->finalBuffer_.isEmpty()) {
                    emit self->commitText(self->finalBuffer_);
                    self->finalBuffer_.clear();
                }
            }
            break;
        case ANYTALK_EVENT_ERROR:
            // Drop any partial accumulation — we don't commit half-recognized
            // text on error. The overlay stays in Error state until the user
            // dismisses it (Esc / F2). The addon's F2 handler calls Hide()
            // and resets its own state machine.
            self->finalBuffer_.clear();
            emit self->stateChanged(QStringLiteral("error"));
            emit self->errorOccurred(s);
            break;
        case ANYTALK_EVENT_LEVEL: {
            bool ok = false;
            const double v = s.toDouble(&ok);
            if (ok) emit self->audioLevel(v);
            break;
        }
        }
    });
}

void AsrController::startRecording() {
    if (!ctx_) return;
    if (starting_.exchange(true)) return;
    auto *ctx = ctx_;
    auto *flag = &starting_;
    (void)QtConcurrent::run([ctx, flag]() {
        anytalk_start(ctx);
        flag->store(false);
    });
}

void AsrController::stopRecording() {
    if (!ctx_) return;
    if (stopping_.exchange(true)) return;
    auto *ctx = ctx_;
    auto *flag = &stopping_;
    (void)QtConcurrent::run([ctx, flag]() {
        anytalk_stop(ctx);
        flag->store(false);
    });
}

void AsrController::cancelRecording() {
    if (!ctx_) return;
    auto *ctx = ctx_;
    (void)QtConcurrent::run([ctx]() { anytalk_cancel(ctx); });
}

QString AsrController::postProcess(const QString &text) const {
    if (!removeTrailingPunctuation_) return text;
    static const QString puncts = QStringLiteral("，。！？、；：,.!?;:");
    QString out = text;
    while (!out.isEmpty() && puncts.contains(out.back())) out.chop(1);
    return out;
}
