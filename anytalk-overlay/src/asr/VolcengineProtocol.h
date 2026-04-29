#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <optional>

namespace volcengine {

// All frames within one connection must carry a strictly increasing positive
// `seq` (mixing seq / no-seq frames triggers "autoAssignedSequence" server
// errors). Caller convention: seq=1 for the first request, 2..N for audio.
QByteArray buildFullClientRequest(const QByteArray &json, qint32 seq);
QByteArray buildAudioOnlyRequest(const QByteArray &pcm, bool last, qint32 seq);

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

/// Build the initial FULL_CLIENT_REQUEST JSON. `enableNonstream` toggles
/// Doubao's two-pass recognition (partials over bidi + finals re-run via
/// nostream). Server-side: only honored when mode == "bidi"; ignored
/// silently elsewhere per docs.
QByteArray buildInitialRequestJson(const QString &mode, bool enableNonstream = false);

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
