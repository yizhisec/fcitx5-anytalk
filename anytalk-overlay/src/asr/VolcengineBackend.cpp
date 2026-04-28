#include "VolcengineBackend.h"

#include <QDebug>
#include <QNetworkRequest>
#include <QUrl>
#include <QWebSocket>

namespace {
constexpr const char *kHost = "openspeech.bytedance.com";

QString pathForMode(const QString &mode) {
    if (mode == QLatin1String("bidi")) return QStringLiteral("/api/v3/sauc/bigmodel");
    if (mode == QLatin1String("bidi_async")) return QStringLiteral("/api/v3/sauc/bigmodel_async");
    return QStringLiteral("/api/v3/sauc/bigmodel_nostream");
}
} // namespace

VolcengineBackend::VolcengineBackend(Settings settings, QObject *parent)
    : AsrBackend(parent), settings_(std::move(settings)) {}

VolcengineBackend::~VolcengineBackend() = default;

void VolcengineBackend::openWebSocket() {
    ws_ = std::make_unique<QWebSocket>();
    connect(ws_.get(), &QWebSocket::connected, this, &VolcengineBackend::onWsConnected);
    connect(ws_.get(), &QWebSocket::binaryMessageReceived, this, &VolcengineBackend::onWsBinary);
    connect(ws_.get(), &QWebSocket::errorOccurred, this, &VolcengineBackend::onWsError);
    connect(ws_.get(), &QWebSocket::disconnected, this, &VolcengineBackend::onWsDisconnected);

    QNetworkRequest req(QUrl(QStringLiteral("wss://%1%2").arg(kHost, pathForMode(settings_.mode))));
    req.setRawHeader("X-Api-App-Key", settings_.appId.toUtf8());
    req.setRawHeader("X-Api-Access-Key", settings_.accessToken.toUtf8());
    req.setRawHeader("X-Api-Resource-Id", settings_.resourceId.toUtf8());
    req.setRawHeader("X-Api-Connect-Id",
                     QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
    ws_->open(req);
}

void VolcengineBackend::start() {
    if (state_ != State::Idle) return;
    parseState_ = {};
    pendingAudio_.clear();
    nextSeq_ = 1;
    state_ = State::Connecting;
    openWebSocket();
}

void VolcengineBackend::pushPcm(const QByteArray &chunk) {
    if (state_ == State::Connecting) {
        // Buffer for onWsConnected() to flush. Cap so a stuck handshake
        // (network down) can't grow the buffer unbounded.
        constexpr int kMaxPendingBytes = 16000 * 2 * 10;  // 10s @ 16kHz S16LE
        if (pendingAudio_.size() < kMaxPendingBytes) {
            pendingAudio_.append(chunk);
        }
        return;
    }
    if (state_ != State::Recording) return;
    if (!ws_ || ws_->state() != QAbstractSocket::ConnectedState) return;
    ws_->sendBinaryMessage(volcengine::buildAudioOnlyRequest(
        chunk, /*last=*/false, nextSeq_++));
}

void VolcengineBackend::stop() {
    if (state_ != State::Recording) return;
    state_ = State::Stopping;
    if (ws_ && ws_->state() == QAbstractSocket::ConnectedState) {
        // Send a final audio frame with the LAST flag so the server knows to drain.
        ws_->sendBinaryMessage(volcengine::buildAudioOnlyRequest(
            QByteArray(), /*last=*/true, nextSeq_++));
    }
    // Server will deliver one or more responses + close; teardown happens in
    // onWsDisconnected / on a final response frame (flags & 0x3 == 0x3).
}

void VolcengineBackend::cancel() {
    if (state_ == State::Idle) return;
    teardown({}); // silent — no error emitted
}

void VolcengineBackend::onWsConnected() {
    if (state_ != State::Connecting) return;
    emit connected();
    state_ = State::Recording;
    const auto initial = volcengine::buildInitialRequestJson(settings_.mode);
    ws_->sendBinaryMessage(volcengine::buildFullClientRequest(initial, nextSeq_++));
    // Flush handshake-buffered audio in 200ms slices — Doubao silently
    // drops audio_only frames much larger than that.
    if (!pendingAudio_.isEmpty()) {
        constexpr int kFlushSliceBytes = 16000 * 2 * 200 / 1000;  // 200ms @ 16kHz S16LE
        for (int off = 0; off < pendingAudio_.size(); off += kFlushSliceBytes) {
            const int len = std::min<int>(kFlushSliceBytes,
                                          pendingAudio_.size() - off);
            ws_->sendBinaryMessage(volcengine::buildAudioOnlyRequest(
                pendingAudio_.mid(off, len), /*last=*/false, nextSeq_++));
        }
        pendingAudio_.clear();
    }
}

void VolcengineBackend::onWsBinary(const QByteArray &data) {
    const auto parsed = volcengine::parseServerFrame(data);
    if (parsed.kind == volcengine::ParsedFrame::Kind::Error) {
        const QString msg = parsed.errorMessage.isEmpty() ? QStringLiteral("server error")
                                                          : parsed.errorMessage;
        teardown(msg);
        return;
    }
    if (parsed.kind != volcengine::ParsedFrame::Kind::Response) return;

    const auto asr = volcengine::parseAsrResponse(parsed.jsonText, parseState_, settings_.mode);
    if (asr.partial.has_value()) emit partial(*asr.partial);
    for (const auto &f : asr.finals) emit final_(f);

    if (parsed.isFinalFrame()) {
        // Server side end-of-recognition.
        teardown({});
    }
}

void VolcengineBackend::onWsError(QAbstractSocket::SocketError) {
    if (state_ == State::Idle) return;
    teardown(ws_ ? ws_->errorString() : QStringLiteral("WebSocket error"));
}

void VolcengineBackend::onWsDisconnected() {
    if (state_ == State::Idle) return;
    // Normal close after the final frame: state already moved through Stopping.
    teardown({});
}

void VolcengineBackend::teardown(const QString &errorMessage) {
    if (ws_) {
        // teardown() can be called from within a QWebSocket signal slot
        // (binaryMessageReceived, errorOccurred, disconnected). Destroying
        // the socket synchronously while Qt's network stack is mid-emit
        // causes a use-after-free inside QAbstractSocket::canReadNotification
        // / qopensslbackend (observed: SIGSEGV with bogus vtable pointer).
        // Detach signals first, then defer destruction to the event loop.
        QWebSocket *raw = ws_.release();
        raw->disconnect(this);
        if (raw->state() != QAbstractSocket::UnconnectedState) raw->close();
        raw->deleteLater();
    }
    const bool wasError = !errorMessage.isEmpty();
    state_ = State::Idle;
    parseState_ = {};
    pendingAudio_.clear();
    if (wasError) emit error(errorMessage);
    else emit finished();
}
