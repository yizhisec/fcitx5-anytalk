#pragma once
#include "AsrBackend.h"
#include "VolcengineProtocol.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QList>
#include <QSslError>
#include <QString>
#include <QTimer>
#include <QUuid>
#include <memory>

class QWebSocket;

/// ASR backend talking to Volcengine (Doubao) bigmodel via wss://.
/// Wire protocol: 4-byte header + 4-byte BE length + payload.
/// Initial frame is FULL_CLIENT_REQUEST (JSON config), subsequent frames
/// are AUDIO_ONLY_REQUEST PCM chunks. Server replies are FULL_SERVER_RESPONSE
/// (JSON) or ERROR_RESPONSE.
class VolcengineBackend : public AsrBackend {
    Q_OBJECT
public:
    struct Settings {
        QString appId;
        QString accessToken;
        QString resourceId = QStringLiteral("volc.seedasr.sauc.duration");
        // Wire-level mode passed to the SAUC endpoint: "bidi" | "bidi_async"
        // | "nostream". The SettingsDialog combobox exposes a fourth UI-only
        // synthetic key "bidi_2pass" which is split into mode="bidi" plus
        // enableNonstream=true on save — that key never reaches this struct.
        QString mode = QStringLiteral("bidi_async");
        // Two-pass: bidi delivers realtime partials, finals are re-recognized
        // via the nostream model for higher accuracy. Doubao docs note this
        // is only supported on the optimized bidi path; the protocol layer
        // gates the JSON insertion to enforce that server-side rule.
        bool enableNonstream = false;
    };

    explicit VolcengineBackend(Settings settings, QObject *parent = nullptr);
    ~VolcengineBackend() override;

    void start() override;
    void pushPcm(const QByteArray &chunk) override;
    void stop() override;
    void cancel() override;

private slots:
    void onWsConnected();
    void onWsBinary(const QByteArray &data);
    void onWsError(QAbstractSocket::SocketError err);
    void onWsDisconnected();
    void onWsSslErrors(const QList<QSslError> &errors);
    void onWsStateChanged(QAbstractSocket::SocketState state);
    void onHandshakeTimeout();

private:
    enum class State { Idle, Connecting, Recording, Stopping };

    void openWebSocket();
    void resetSession();
    void teardown(const QString &errorMessage);

    Settings settings_;
    std::unique_ptr<QWebSocket> ws_;
    State state_ = State::Idle;

    volcengine::AsrParseState parseState_;

    // Audio captured during ws handshake; flushed in onWsConnected() so the
    // user's leading words aren't dropped.
    QByteArray pendingAudio_;

    // Per-connection sequence: full client request gets 1, audio frames 2..N.
    // The protocol rejects mixed seq/no-seq frames within one connection.
    qint32 nextSeq_ = 1;

    // QWebSocket has no built-in handshake timeout — a TLS-completed but
    // upgrade-stuck server would hang in Connecting forever. Fires
    // teardown() with a clear error so the UI can recover.
    QTimer handshakeTimer_;
};
