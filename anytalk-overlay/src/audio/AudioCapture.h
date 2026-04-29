#pragma once
#include <QByteArray>
#include <QObject>
#include <QThread>
#include <atomic>

/// 16-bit little-endian, 16 kHz, mono PCM capture.
/// Emits 40 ms (1280 byte / 640 sample) chunks; emits an RMS level
/// estimate (~25 Hz). Backed by libpulse-simple on Linux.
/// One PA stream per object lifetime: start() opens, stop()/dtor release.
class AudioCapture : public QObject {
    Q_OBJECT
public:
    static constexpr int kSampleRate = 16000;
    static constexpr int kChunkBytes = 1280; // 40 ms @ 16 kHz mono S16LE

    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture() override;

    /// Spawns the capture thread; pa_simple_new() runs inside it so the
    /// caller can overlap PA open (~200 ms) and warm-up (~1 s zero-padding)
    /// with other startup work. Errors arrive via the `error` signal.
    bool start();

    /// Tear down the PA stream and the read thread so the kernel actually
    /// releases the source (and any BT SCO link). Safe to call multiple
    /// times; safe to call from the destructor.
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
    /// Stop the read thread and release the pa_simple stream. Bounded
    /// wait — leaks the thread + pa_simple if PA is wedged so the caller
    /// (stop() or ~AudioCapture()) doesn't deadlock.
    void teardownStream();
    static double computeRms(const QByteArray &pcm16le);

    QThread *thread_ = nullptr;
    std::atomic_bool running_{false};  // thread should keep reading
    std::atomic_bool active_{false};   // forward reads to listeners
    std::atomic_bool warmedUp_{false}; // first non-silent chunk seen, sticky
    void *pa_ = nullptr;               // pa_simple* (kept opaque)
};
