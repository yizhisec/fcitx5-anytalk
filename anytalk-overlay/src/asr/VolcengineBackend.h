#pragma once
#include "AsrBackend.h"
#include "VolcengineProtocol.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QString>
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
        QString mode = QStringLiteral("bidi_async");
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

private:
    enum class State { Idle, Connecting, Recording, Stopping };

    void openWebSocket();
    void resetSession();
    void teardown(const QString &errorMessage);

    Settings settings_;
    std::unique_ptr<QWebSocket> ws_;
    State state_ = State::Idle;

    volcengine::AsrParseState parseState_;
};
