#include "AudioCapture.h"

#include <QDebug>
#include <pulse/error.h>
#include <pulse/simple.h>
#include <cmath>

AudioCapture::AudioCapture(QObject *parent) : QObject(parent) {}

AudioCapture::~AudioCapture() { stop(); }

bool AudioCapture::start() {
    if (running_.load(std::memory_order_acquire)) return true;

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

void AudioCapture::stop() {
    // Note: guarding stop() with `if (!running_.exchange(false)) return;`
    // is wrong because the capture loop itself can flip running_ to false
    // on a read error. Always tear resources down — both checks are
    // null-guarded so calling stop() twice is harmless.
    running_.store(false, std::memory_order_release);
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

void AudioCapture::captureLoop() {
    QByteArray buf;
    buf.resize(kChunkBytes);
    auto *pa = static_cast<pa_simple *>(pa_);
    while (running_.load(std::memory_order_acquire)) {
        int err = 0;
        if (pa_simple_read(pa, buf.data(), buf.size(), &err) < 0) {
            qWarning() << "AudioCapture: pa_simple_read failed:" << pa_strerror(err);
            emit error(QStringLiteral("音频读取失败"));
            running_.store(false, std::memory_order_release);
            break;
        }
        emit pcm(buf);
        emit level(computeRms(buf));
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
