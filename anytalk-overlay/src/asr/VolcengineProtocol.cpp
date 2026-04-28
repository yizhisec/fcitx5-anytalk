#include "VolcengineProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QtEndian>

namespace volcengine {

namespace {
constexpr quint8 kVersion          = 0b0001;
constexpr quint8 kHeaderSize4B     = 0b0001;
constexpr quint8 kMsgFullClientReq = 0b0001;
constexpr quint8 kMsgAudioOnly     = 0b0010;
constexpr quint8 kMsgFullServerRsp = 0b1001;
constexpr quint8 kMsgErrorResp     = 0b1111;
constexpr quint8 kFlagPosSeq       = 0b0001;  // intermediate frame
constexpr quint8 kFlagNegWithSeq   = 0b0011;  // last frame, seq negated
constexpr quint8 kSerJson          = 0b0001;
constexpr quint8 kSerNone          = 0b0000;
constexpr quint8 kCompressionNone  = 0b0000;

QByteArray buildHeader(quint8 messageType, quint8 flags, quint8 serialization,
                       quint8 compression) {
    QByteArray h(4, 0);
    h[0] = static_cast<char>(((kVersion & 0xF) << 4) | (kHeaderSize4B & 0xF));
    h[1] = static_cast<char>(((messageType & 0xF) << 4) | (flags & 0xF));
    h[2] = static_cast<char>(((serialization & 0xF) << 4) | (compression & 0xF));
    h[3] = 0;
    return h;
}

QByteArray u32be(quint32 n) {
    QByteArray b(4, 0);
    qToBigEndian(n, b.data());
    return b;
}
} // namespace

QByteArray buildFullClientRequest(const QByteArray &json, qint32 seq) {
    // Wire layout: 4B header + 4B sequence (BE int32) + 4B payload size + JSON.
    // Tagging this frame with a sequence is required as soon as any subsequent
    // frame in the same connection uses POS_SEQUENCE — otherwise the server
    // tries to auto-assign one and fails with "decode V1 protocol message
    // autoAssignedSequence".
    QByteArray out;
    out.reserve(12 + json.size());
    out.append(buildHeader(kMsgFullClientReq, kFlagPosSeq, kSerJson, kCompressionNone));
    out.append(u32be(static_cast<quint32>(seq)));
    out.append(u32be(static_cast<quint32>(json.size())));
    out.append(json);
    return out;
}

QByteArray buildAudioOnlyRequest(const QByteArray &pcm, bool last, qint32 seq) {
    // Wire layout: 4B header + 4B sequence (BE int32, negated on last frame)
    // + 4B payload size + raw PCM. NEG_WITH_SEQUENCE on last so the server
    // sees a clear end-of-stream; POS_SEQUENCE on intermediate frames so
    // burst-flushed audio doesn't get reordered/dropped.
    const quint8 flags = last ? kFlagNegWithSeq : kFlagPosSeq;
    const qint32 wireSeq = last ? -seq : seq;

    QByteArray out;
    out.reserve(12 + pcm.size());
    out.append(buildHeader(kMsgAudioOnly, flags, kSerNone, kCompressionNone));
    out.append(u32be(static_cast<quint32>(wireSeq)));
    out.append(u32be(static_cast<quint32>(pcm.size())));
    out.append(pcm);
    return out;
}

ParsedFrame parseServerFrame(const QByteArray &data) {
    ParsedFrame f;
    if (data.size() < 4) return f;

    const auto b0 = static_cast<uint8_t>(data[0]);
    const auto b1 = static_cast<uint8_t>(data[1]);
    if (((b0 >> 4) & 0xF) != kVersion || (b0 & 0xF) != kHeaderSize4B) return f;

    const quint8 messageType = (b1 >> 4) & 0xF;
    f.flags = b1 & 0xF;

    if (data.size() < 12) return f;

    if (messageType == kMsgFullServerRsp) {
        const auto payloadSize =
            qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + 8));
        if (data.size() < static_cast<int>(12 + payloadSize)) return f;
        f.kind = ParsedFrame::Kind::Response;
        f.jsonText = data.mid(12, payloadSize);
        return f;
    }

    if (messageType == kMsgErrorResp) {
        const auto code =
            qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + 4));
        const auto msgSize =
            qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + 8));
        if (data.size() < static_cast<int>(12 + msgSize)) return f;
        f.kind = ParsedFrame::Kind::Error;
        f.errorCode = code;
        f.errorMessage = QString::fromUtf8(data.mid(12, msgSize));
        return f;
    }
    return f;
}

QByteArray buildInitialRequestJson(const QString &mode) {
    const bool isNoStream = (mode == QLatin1String("nostream"));
    QJsonObject audio{
        {"format", "pcm"}, {"rate", 16000}, {"bits", 16}, {"channel", 1}};
    if (isNoStream) audio.insert("language", "zh-CN");

    QJsonObject request{
        {"model_name", "bigmodel"},
        {"enable_itn", true},
        {"enable_punc", true},
        {"enable_ddc", false},
        {"enable_word", false},
        {"res_type", "full"},
        {"nbest", 1},
        {"use_vad", true},
    };

    QJsonObject root{
        {"user", QJsonObject{{"uid", "anytalk"}}},
        {"audio", audio},
        {"request", request},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

AsrParsed parseAsrResponse(const QByteArray &jsonBytes, AsrParseState &state,
                            const QString &mode) {
    AsrParsed result;

    QJsonParseError perr{};
    const auto doc = QJsonDocument::fromJson(jsonBytes, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) return result;

    const auto root = doc.object();
    const auto resultVal = root.value(QStringLiteral("result"));
    if (!resultVal.isObject()) return result;
    const auto resultObj = resultVal.toObject();

    auto trim = [](const QString &s) { return s.trimmed(); };

    if (resultObj.contains(QStringLiteral("utterances"))) {
        const auto utterancesVal = resultObj.value(QStringLiteral("utterances"));
        if (utterancesVal.isArray()) {
            const auto utterances = utterancesVal.toArray();

            // Definite (final) utterances, dedup by end_time.
            for (const auto &uVal : utterances) {
                if (!uVal.isObject()) continue;
                const auto u = uVal.toObject();
                const bool definite = u.value(QStringLiteral("definite")).toBool(false);
                if (!definite) continue;
                const qint64 endTime = u.value(QStringLiteral("end_time")).toVariant().toLongLong();
                if (endTime <= state.lastCommittedEndTime) continue;
                const QString text = trim(u.value(QStringLiteral("text")).toString());
                if (text.isEmpty()) continue;
                result.finals.append(text);
                state.lastCommittedEndTime = endTime;
            }

            // Last non-definite utterance becomes the partial.
            for (int i = utterances.size() - 1; i >= 0; --i) {
                if (!utterances.at(i).isObject()) continue;
                const auto u = utterances.at(i).toObject();
                const bool definite = u.value(QStringLiteral("definite")).toBool(false);
                if (definite) continue;
                const QString text = trim(u.value(QStringLiteral("text")).toString());
                if (text.isEmpty()) continue;
                result.partial = text;
                break;
            }
            return result;
        }
    }

    // Fallback: result.text (no utterances array).
    const auto fullText = trim(resultObj.value(QStringLiteral("text")).toString());
    if (fullText.isEmpty()) return result;

    if (mode == QLatin1String("bidi_async")) {
        result.partial = fullText;
        result.finals.append(fullText);
    } else if (!state.lastFullText.isEmpty() && fullText.startsWith(state.lastFullText)) {
        const QString suffix = trim(fullText.mid(state.lastFullText.size()));
        if (!suffix.isEmpty()) result.finals.append(suffix);
    } else if (fullText != state.lastFullText) {
        result.finals.append(fullText);
    }
    state.lastFullText = fullText;
    return result;
}

} // namespace volcengine
