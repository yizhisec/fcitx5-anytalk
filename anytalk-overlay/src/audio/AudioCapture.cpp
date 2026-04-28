#include "AudioCapture.h"

#include <QDebug>
#include <pulse/error.h>
#include <pulse/simple.h>
#include <cmath>

AudioCapture::AudioCapture(QObject *parent) : QObject(parent) {}

AudioCapture::~AudioCapture() {
    running_.store(false, std::memory_order_release);
    active_.store(false, std::memory_order_release);
    if (thread_) {
        thread_->wait();
        thread_->deleteLater();
        thread_ = nullptr;
    }
    if (pa_) {
        pa_simple_free(static_cast<pa_simple *>(pa_));
        pa_ = nullptr;
    }
}

bool AudioCapture::prewarm() {
    if (pa_ && running_.load(std::memory_order_acquire)) return true;

    // Clean up after a dead thread (read failed in captureLoop).
    if (thread_) {
        thread_->wait();
        thread_->deleteLater();
        thread_ = nullptr;
    }
    if (pa_) {
        pa_simple_free(static_cast<pa_simple *>(pa_));
        pa_ = nullptr;
    }
    warmedUp_.store(false, std::memory_order_release);

    pa_sample_spec spec{};
    spec.format = PA_SAMPLE_S16LE;
    spec.rate = kSampleRate;
    spec.channels = 1;

    pa_buffer_attr attr{};
    attr.maxlength = static_cast<uint32_t>(-1);
    attr.tlength = static_cast<uint32_t>(-1);
    attr.prebuf = static_cast<uint32_t>(-1);
    attr.minreq = static_cast<uint32_t>(-1);
    attr.fragsize = kChunkBytes;

    int paErr = 0;
    auto *pa = pa_simple_new(nullptr, "anytalk", PA_STREAM_RECORD, nullptr,
                              "Voice Input", &spec, nullptr, &attr, &paErr);
    if (!pa) {
        qWarning() << "AudioCapture: pa_simple_new failed:" << pa_strerror(paErr);
        emit error(QStringLiteral("麦克风不可用，请检查 PulseAudio/PipeWire 或音频设备"));
        return false;
    }

    pa_ = pa;
    running_.store(true, std::memory_order_release);
    thread_ = QThread::create([this] { captureLoop(); });
    thread_->setObjectName(QStringLiteral("anytalk-capture"));
    thread_->start();
    return true;
}

bool AudioCapture::start() {
    // Rebuild if the previous stream died in captureLoop.
    if (!pa_ || !running_.load(std::memory_order_acquire)) {
        if (!prewarm()) return false;
    }
    active_.store(true, std::memory_order_release);
    return true;
}

void AudioCapture::stop() {
    // Keep the PA stream open and the thread reading (discarding output) so
    // the source doesn't suspend — that suspend ramp-up costs ~1s of zero
    // padding on the next start() and is the single biggest hit to
    // first-press responsiveness when switching applications.
    active_.store(false, std::memory_order_release);
}

void AudioCapture::captureLoop() {
    QByteArray buf;
    buf.resize(kChunkBytes);
    auto *pa = static_cast<pa_simple *>(pa_);
    while (running_.load(std::memory_order_acquire)) {
        int err = 0;
        if (pa_simple_read(pa, buf.data(), buf.size(), &err) < 0) {
            // PulseAudio occasionally recycles long-lived streams; surfacing
            // an error mid-recording would be confusing, so only emit when
            // a session is actually live. Either way the next start() will
            // detect the dead thread and rebuild.
            qWarning() << "AudioCapture: pa_simple_read failed:" << pa_strerror(err);
            if (active_.load(std::memory_order_acquire)) {
                emit error(QStringLiteral("音频读取失败"));
            }
            running_.store(false, std::memory_order_release);
            break;
        }
        const double rms = computeRms(buf);
        if (!warmedUp_.load(std::memory_order_acquire) && rms > 1e-4) {
            warmedUp_.store(true, std::memory_order_release);
            emit warmedUp();
        }
        if (active_.load(std::memory_order_acquire)) {
            emit pcm(buf);
            emit level(rms);
        }
    }
}

double AudioCapture::computeRms(const QByteArray &pcm16le) {
    const qsizetype n = pcm16le.size() / 2;
    if (n == 0) return 0.0;
    // S16LE matches host int16_t on x86 / aarch64. If we ever ship on a
    // big-endian target, swap to qFromLittleEndian here.
    const auto *data = reinterpret_cast<const int16_t *>(pcm16le.constData());
    double sumSq = 0.0;
    for (qsizetype i = 0; i < n; ++i) {
        const double v = static_cast<double>(data[i]) / 32768.0;
        sumSq += v * v;
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(n));
    // Map typical voice RMS [0, 0.4] → [0, 1] for the bars.
    return std::clamp(rms / 0.4, 0.0, 1.0);
}
