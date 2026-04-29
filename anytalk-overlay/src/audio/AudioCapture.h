#pragma once
#include <QByteArray>
#include <QObject>
#include <QThread>
#include <atomic>

/// 16-bit little-endian, 16 kHz, mono PCM capture.
/// Emits 40 ms (1280 byte / 640 sample) chunks; emits an RMS level
/// estimate (~25 Hz). Backed by libpulse-simple on Linux.
///
/// Two lifecycle modes (chosen via setOnDemand()):
///  - always-on: PA stream + read thread persist for the life of the
///    object; start()/stop() only flip an active_ flag. Avoids PA's
///    idle-source suspend ramp (~1 s of zero-padding) so first-press
///    responsiveness is good. Safe for ALSA / USB mics.
///  - on-demand: stop() tears the stream down; start() rebuilds it.
///    Required for Bluetooth HFP/SCO mics — see CLAUDE.md "Bluetooth
///    mic warning". Trades 1 s of first-press silence for not pinning
///    a kernel-side SCO link the user might need to release safely.
class AudioCapture : public QObject {
    Q_OBJECT
public:
    static constexpr int kSampleRate = 16000;
    static constexpr int kChunkBytes = 1280; // 40 ms @ 16 kHz mono S16LE

    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture() override;

    /// Choose whether the PA stream stays open while idle.
    /// - false (default, "always-on"): stream + read thread persist for the
    ///   life of the object; stop() only flips a flag. Best first-press
    ///   responsiveness, safe for built-in / wired / USB mics.
    /// - true ("on-demand"): stop() tears down the stream and read thread;
    ///   start() rebuilds them. Required for Bluetooth HFP/SCO mics — see
    ///   CLAUDE.md "Bluetooth mic warning"; long-lived SCO + close trips a
    ///   kernel race that can wedge the system.
    /// Should be set before prewarm()/start(); changing it later only
    /// affects the next stop()/start() cycle.
    void setOnDemand(bool onDemand);
    bool isOnDemand() const { return onDemand_.load(std::memory_order_acquire); }

    /// Open the PA stream and spawn the read thread. Idempotent. Call once
    /// at process startup so the first F2 doesn't pay for stream creation.
    /// Returns false if PulseAudio is unavailable (mic missing, server down).
    bool prewarm();

    /// Mark the capture as active so subsequent PCM is forwarded. Lazily
    /// calls prewarm() if the stream isn't up yet (always-on mode skips
    /// this when the stream is already live; on-demand always rebuilds).
    /// Returns false if the underlying PA stream cannot be opened.
    bool start();

    /// In always-on mode: flip active_ off; the thread keeps reading and
    /// discarding so the PA source doesn't suspend.
    /// In on-demand mode: tear down the PA stream and the read thread so
    /// the kernel actually releases the source (and any BT SCO link).
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
    std::atomic_bool onDemand_{false}; // stop() really releases the stream
    void *pa_ = nullptr;               // pa_simple* (kept opaque)
};
