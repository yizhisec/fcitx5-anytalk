#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <optional>

namespace volcengine {

QByteArray buildFullClientRequest(const QByteArray &json);
QByteArray buildAudioOnlyRequest(const QByteArray &pcm, bool last);

struct ParsedFrame {
    enum class Kind { Unknown, Response, Error };
    Kind kind = Kind::Unknown;
    quint8 flags = 0;
    QByteArray jsonText;        // when kind == Response
    quint32 errorCode = 0;       // when kind == Error
    QString errorMessage;        // when kind == Error
    bool isFinalFrame() const { return (flags & 0x3) == 0x3; } // 0b0011
};

ParsedFrame parseServerFrame(const QByteArray &data);

QByteArray buildInitialRequestJson(const QString &mode);

struct AsrParseState {
    qint64 lastCommittedEndTime = -1;
    QString lastFullText;
};

struct AsrParsed {
    std::optional<QString> partial;
    QStringList finals;
};

/// Parse a server JSON payload, extracting partial / finals.
/// Stateful: caller persists `state` across messages within a session.
AsrParsed parseAsrResponse(const QByteArray &json, AsrParseState &state, const QString &mode);

} // namespace volcengine
