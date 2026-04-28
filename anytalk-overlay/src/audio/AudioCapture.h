#pragma once
#include <QByteArray>
#include <QObject>
#include <QThread>
#include <atomic>

/// 16-bit little-endian, 16 kHz, mono PCM capture.
/// Emits 40 ms (1280 byte / 640 sample) chunks; emits an RMS level
/// estimate (~25 Hz). Backed by libpulse-simple on Linux.
///
/// The PulseAudio stream stays open for the lifetime of the object once
/// started; the capture thread reads continuously to keep the system source
/// awake (otherwise PA suspends idle sources after a few seconds and the
/// next stream start ships ~1 s of zero-padding). `start()` and `stop()`
/// only flip an active_ flag controlling whether reads are forwarded.
class AudioCapture : public QObject {
    Q_OBJECT
public:
    static constexpr int kSampleRate = 16000;
    static constexpr int kChunkBytes = 1280; // 40 ms @ 16 kHz mono S16LE

    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture() override;

    /// Open the PA stream and spawn the read thread. Idempotent. Call once
    /// at process startup so the first F2 doesn't pay for stream creation.
    /// Returns false if PulseAudio is unavailable (mic missing, server down).
    bool prewarm();

    /// Mark the capture as active so subsequent PCM is forwarded. Lazily
    /// calls prewarm() if the stream isn't up yet. Returns false if the
    /// underlying PA stream cannot be opened.
    bool start();

    /// Mark the capture inactive. The thread keeps reading (and discarding)
    /// so the PA source stays awake and the next start() is instantaneous.
    void stop();

    bool isActive() const { return active_.load(std::memory_order_acquire); }

    /// True once the underlying PA stream has produced its first non-silent
    /// chunk (i.e. the source has finished its zero-padding ramp-up). Sticky.
    bool isWarmedUp() const { return warmedUp_.load(std::memory_order_acquire); }

signals:
    void pcm(const QByteArray &chunk);
    void level(double rms);  // 0..1
    void error(const QString &msg);
    /// Emitted once, when the first non-silent PCM chunk arrives. Lets the
    /// controller hold off the "Recording" UI state until the mic is really
    /// awake.
    void warmedUp();

private:
    void captureLoop();
    static double computeRms(const QByteArray &pcm16le);

    QThread *thread_ = nullptr;
    std::atomic_bool running_{false};  // thread should keep reading
    std::atomic_bool active_{false};   // forward reads to listeners
    std::atomic_bool warmedUp_{false}; // first non-silent chunk seen, sticky
    void *pa_ = nullptr;               // pa_simple* (kept opaque)
};
