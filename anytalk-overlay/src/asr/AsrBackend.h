#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>

/// Abstract ASR engine. Concrete backends (Volcengine, OpenAI, local
/// whisper.cpp, …) implement this. AsrController owns one instance, drives
/// it from AudioCapture, and forwards its signals to the rest of the app.
///
/// Lifecycle invariants:
///   start() → 0+ pushPcm() → stop()  (final) emitted, then idle
///   start() → 0+ pushPcm() → cancel()  (no final emitted)
///   error()  may fire at any time; backend transitions back to idle.
class AsrBackend : public QObject {
    Q_OBJECT
public:
    explicit AsrBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~AsrBackend() override = default;

    /// Begin a new recognition session.
    virtual void start() = 0;

    /// Submit a 16-bit LE 16 kHz mono PCM chunk.
    virtual void pushPcm(const QByteArray &chunk) = 0;

    /// Signal end-of-speech. Backend should drain any pending finals
    /// before returning to idle.
    virtual void stop() = 0;

    /// Discard the in-flight session without producing a final.
    virtual void cancel() = 0;

signals:
    /// Streaming partial transcript. Backends without partial support never emit.
    void partial(const QString &text);
    /// A stable transcript segment. May fire multiple times in a session.
    void final_(const QString &text);
    /// Human-readable error. Backend is back to idle after this.
    void error(const QString &message);
    /// Connection ready / first frame of the session can flow.
    /// Backends that have no "connect" step (e.g. local whisper.cpp) emit
    /// this immediately after start().
    void connected();
    /// Session ended cleanly — all in-flight finals have been emitted and
    /// the backend is idle. AsrController commits the accumulated text on
    /// receiving this. Mutually exclusive with error().
    void finished();
};
