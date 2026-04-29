#pragma once
#include <QObject>
#include <QString>
#include <memory>

class AsrBackend;
class AudioCapture;
struct OverlayConfig;

/// Wires AudioCapture (mic input) and an AsrBackend (transcription engine)
/// together; presents a uniform set of Qt signals to the rest of the app.
/// Backend-specific knowledge stays inside the AsrBackend implementation.
/// Owned by main(); single session per process.
class AsrController : public QObject {
    Q_OBJECT
public:
    explicit AsrController(QObject *parent = nullptr);
    ~AsrController() override;

    /// Plug in a fresh config. Replaces current backend if necessary.
    /// Returns false if the configured backend cannot be instantiated
    /// (missing credentials, unknown backend name).
    bool applyConfig(const OverlayConfig &cfg);

    /// Best-effort post-processing applied to a final segment before
    /// commit (e.g. trailing punctuation removal).
    QString postProcess(const QString &text) const;

public slots:
    void startRecording();
    void stopRecording();
    void cancelRecording();
    /// Idempotent toggle for the dumb-forward fcitx5 addon: starts a new
    /// session if idle/error, otherwise stops the active one.
    void toggleRecording();

signals:
    /// Mirrors backend events for the UI / D-Bus surface.
    void transcriptPartial(const QString &text);
    void transcriptFinal(const QString &text);
    void stateChanged(const QString &state); // idle / connecting / recording / error
    void audioLevel(double level);            // 0..1, ~25 Hz
    void errorOccurred(const QString &text);

    /// Final accumulated transcript ready to be committed (one shot per session).
    void commitText(const QString &text);
    /// Cancellation completed (no commit, no error). Drives short-lived
    /// overlay's exit on Esc/cancel paths.
    void cancelled();

private:
    void onAudioPcm(const QByteArray &chunk);
    void onAudioLevel(double level);
    void onAudioError(const QString &msg);
    void onAudioWarmedUp();

    void onBackendPartial(const QString &text);
    void onBackendFinal(const QString &text);
    void onBackendConnected();
    void onBackendFinished();
    void onBackendError(const QString &msg);

    void maybeEnterRecording();
    void enterIdle(bool fromError);

    std::unique_ptr<AudioCapture> audio_;
    std::unique_ptr<AsrBackend> backend_;

    bool removeTrailingPunctuation_ = false;
    QString currentState_ = QStringLiteral("idle");
    QString finalBuffer_;
    qint64 lastLevelEmitMs_ = 0;
    double lastEmittedLevel_ = -1.0;  // sentinel: never matches a [0,1] bucket
    // Recording = ws connected AND mic produced real audio. Both flags are
    // set by their respective callbacks; the transition happens in
    // maybeEnterRecording() once both are true.
    bool wsConnected_ = false;
    bool audioWarmedUp_ = false;
};
