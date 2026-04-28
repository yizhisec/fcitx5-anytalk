#pragma once
#include "Config.h"
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>

extern "C" {
#include "anytalk_api.h"
}

/// Owns the libanytalk.so engine: audio capture + WebSocket ASR.
/// Translates the C-ABI callback (which fires on a non-Qt thread) into Qt
/// signals dispatched on the main thread via Qt::QueuedConnection.
class AsrController : public QObject {
    Q_OBJECT
public:
    explicit AsrController(QObject *parent = nullptr);
    ~AsrController() override;

    /// Initialises libanytalk with the given config. Returns false if the
    /// engine could not be created.
    bool initialise(const OverlayConfig &cfg);

    /// Helper: trims punctuation per user setting (matches the addon's prior
    /// behaviour).
    QString postProcess(const QString &text) const;

public slots:
    void startRecording();
    void stopRecording();
    void cancelRecording();

signals:
    /// Mirrors anytalk events as Qt signals.
    void transcriptPartial(const QString &text);
    void transcriptFinal(const QString &text);
    void stateChanged(const QString &state);
    void audioLevel(double level);
    void errorOccurred(const QString &text);

    /// Emitted when a session finishes naturally with non-empty text. The
    /// payload is the full accumulated transcript (post-processed). The
    /// addon turns this into a single commitString() — no streaming preedit.
    /// Future post-processing steps (LLM polish, translation, etc.) plug in
    /// before this signal fires.
    void commitText(const QString &text);

private:
    static void zigCallback(void *user_data, AnytalkEventType type, const char *text);

    AnytalkContext *ctx_ = nullptr;
    bool removeTrailingPunctuation_ = false;
    std::atomic_bool starting_{false};
    std::atomic_bool stopping_{false};

    // Main-thread state (touched only inside QMetaObject::invokeMethod hops).
    QString finalBuffer_;  // accumulates all FINAL segments of the current session
};
