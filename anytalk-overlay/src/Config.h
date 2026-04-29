#pragma once
#include <QString>
#include <QVariantHash>

/// User configuration loaded from ~/.config/fcitx5/conf/anytalk.conf.
///
/// New schema (recommended):
///   [Asr]
///   Backend = volcengine          ; volcengine | openai | local-whisper | ...
///   RemoveTrailingPunctuation = false
///
///   [Volcengine]
///   AppID = ...
///   AccessToken = ...
///   Mode = bidi_async             ; optional
///
///   [OpenAI]                      ; future
///   ApiKey = sk-...
///   Model  = gpt-4o-mini-transcribe
///
/// Legacy flat schema (still read for backwards compatibility):
///   AppID                = ...
///   AccessToken          = ...
///   RemoveTrailingPunctuation = false

struct OverlayConfig {
    // Cross-backend
    QString backend = QStringLiteral("volcengine");
    bool removeTrailingPunctuation = false;

    // Per-backend bag — each backend pulls the keys it needs.
    // Stored flat as "Section/Key" → string.
    QVariantHash backendOptions;

    /// Helpers for typed access.
    QString str(const QString &section, const QString &key,
                const QString &fallback = {}) const;
    bool boolean(const QString &section, const QString &key, bool fallback = false) const;

    /// True when the active backend has the bare-minimum credentials it
    /// needs to start a session. Used to decide whether to launch the
    /// SettingsDialog instead of recording.
    bool isUsable() const;

    static QString configFilePath();
    static OverlayConfig load();
    bool save() const;
};
