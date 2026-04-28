#pragma once
#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <atomic>

/// 16-bit little-endian, 16 kHz, mono PCM capture.
/// Emits 40 ms (1280 byte / 640 sample) chunks; emits an RMS level
/// estimate (~25 Hz). Backed by libpulse-simple on Linux.
class AudioCapture : public QObject {
    Q_OBJECT
public:
    static constexpr int kSampleRate = 16000;
    static constexpr int kChunkBytes = 1280; // 40 ms @ 16 kHz mono S16LE

    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture() override;

    /// Starts the capture thread. Idempotent. Returns false if PulseAudio
    /// could not be opened (microphone missing, server down, etc.).
    bool start();

    /// Stops the capture thread synchronously.
    void stop();

    bool isRunning() const { return running_.load(std::memory_order_acquire); }

signals:
    void pcm(const QByteArray &chunk);
    void level(double rms);  // 0..1
    void error(const QString &msg);

private:
    void captureLoop();
    static double computeRms(const QByteArray &pcm16le);

    QThread *thread_ = nullptr;
    std::atomic_bool running_{false};
    void *pa_ = nullptr; // pa_simple* (kept opaque to avoid pulse headers in .h)
};
