#include "Config.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStringBuilder>
#include <QTextStream>

namespace {
constexpr const char *kConfigSubpath = "/.config/fcitx5/conf/anytalk.conf";

QString joinKey(const QString &section, const QString &key) {
    if (section.isEmpty()) return key;
    return section % QLatin1Char('/') % key;
}

bool toBool(const QString &v, bool fallback) {
    const QString s = v.trimmed().toLower();
    if (s == QLatin1String("true") || s == QLatin1String("1") ||
        s == QLatin1String("yes") || s == QLatin1String("on"))
        return true;
    if (s == QLatin1String("false") || s == QLatin1String("0") ||
        s == QLatin1String("no") || s == QLatin1String("off"))
        return false;
    return fallback;
}

CaptureMode parseCaptureMode(const QString &v, CaptureMode fallback) {
    const QString s = v.trimmed().toLower();
    if (s == QLatin1String("auto")) return CaptureMode::Auto;
    if (s == QLatin1String("always-on") || s == QLatin1String("alwayson") ||
        s == QLatin1String("always_on"))
        return CaptureMode::AlwaysOn;
    if (s == QLatin1String("on-demand") || s == QLatin1String("ondemand") ||
        s == QLatin1String("on_demand"))
        return CaptureMode::OnDemand;
    return fallback;
}

QString captureModeToString(CaptureMode m) {
    switch (m) {
        case CaptureMode::AlwaysOn: return QStringLiteral("always-on");
        case CaptureMode::OnDemand: return QStringLiteral("on-demand");
        case CaptureMode::Auto: break;
    }
    return QStringLiteral("auto");
}
} // namespace

QString OverlayConfig::configFilePath() {
    return QDir::homePath() + QString::fromLatin1(kConfigSubpath);
}

QString OverlayConfig::str(const QString &section, const QString &key,
                            const QString &fallback) const {
    const auto v = backendOptions.value(joinKey(section, key));
    return v.isValid() ? v.toString() : fallback;
}

bool OverlayConfig::boolean(const QString &section, const QString &key, bool fallback) const {
    const auto v = backendOptions.value(joinKey(section, key));
    return v.isValid() ? toBool(v.toString(), fallback) : fallback;
}

bool OverlayConfig::isUsable() const {
    if (backend == QLatin1String("volcengine")) {
        return !str(QStringLiteral("Volcengine"), QStringLiteral("AppID")).isEmpty() &&
               !str(QStringLiteral("Volcengine"), QStringLiteral("AccessToken")).isEmpty();
    }
    if (backend == QLatin1String("openai")) {
        return !str(QStringLiteral("OpenAI"), QStringLiteral("ApiKey")).isEmpty();
    }
    return true; // unknown / local backends decide on their own
}

OverlayConfig OverlayConfig::load() {
    OverlayConfig cfg;
    QFile f(configFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return cfg;
    }
    QTextStream in(&f);

    // Legacy flat fields, read first then potentially overwritten by sectioned values.
    QString legacyAppId, legacyToken;
    bool legacyHasRemoveTrailing = false;

    QString currentSection;
    while (!in.atEnd()) {
        const QString raw = in.readLine().trimmed();
        if (raw.isEmpty() || raw.startsWith(QLatin1Char('#'))) continue;
        if (raw.startsWith(QLatin1Char('[')) && raw.endsWith(QLatin1Char(']'))) {
            currentSection = raw.mid(1, raw.size() - 2).trimmed();
            continue;
        }
        const int eq = raw.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = raw.left(eq).trimmed();
        const QString val = raw.mid(eq + 1).trimmed();

        if (currentSection.isEmpty()) {
            // Legacy flat layout.
            if (key == QLatin1String("AppID")) legacyAppId = val;
            else if (key == QLatin1String("AccessToken")) legacyToken = val;
            else if (key == QLatin1String("RemoveTrailingPunctuation")) {
                cfg.removeTrailingPunctuation = toBool(val, false);
                legacyHasRemoveTrailing = true;
            }
            continue;
        }

        if (currentSection == QLatin1String("Asr")) {
            if (key == QLatin1String("Backend")) {
                if (!val.isEmpty()) cfg.backend = val;
            } else if (key == QLatin1String("RemoveTrailingPunctuation")) {
                cfg.removeTrailingPunctuation = toBool(val, false);
                legacyHasRemoveTrailing = true;
            } else {
                cfg.backendOptions.insert(joinKey(currentSection, key), val);
            }
        } else if (currentSection == QLatin1String("Audio")) {
            if (key == QLatin1String("CaptureMode")) {
                cfg.captureMode = parseCaptureMode(val, CaptureMode::Auto);
            } else {
                cfg.backendOptions.insert(joinKey(currentSection, key), val);
            }
        } else {
            cfg.backendOptions.insert(joinKey(currentSection, key), val);
        }
    }

    // Backfill legacy flat fields into [Volcengine] when sectioned values weren't
    // provided. This lets old anytalk.conf files keep working untouched.
    auto fill = [&](const QString &k, const QString &v) {
        if (v.isEmpty()) return;
        const QString full = joinKey(QStringLiteral("Volcengine"), k);
        if (!cfg.backendOptions.contains(full)) cfg.backendOptions.insert(full, v);
    };
    fill(QStringLiteral("AppID"), legacyAppId);
    fill(QStringLiteral("AccessToken"), legacyToken);
    Q_UNUSED(legacyHasRemoveTrailing);

    return cfg;
}

bool OverlayConfig::save() const {
    const QString path = configFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);

    out << "# anytalk-overlay configuration\n";
    out << "# Edit via the SettingsDialog; the file format follows fcitx5 INI.\n\n";
    out << "[Asr]\n";
    out << "Backend = " << backend << "\n";
    out << "RemoveTrailingPunctuation = " << (removeTrailingPunctuation ? "True" : "False") << "\n";

    out << "\n[Audio]\n";
    out << "# auto = probe default source; on-demand for Bluetooth, always-on otherwise.\n";
    out << "# always-on = best first-press latency; unsafe for Bluetooth HFP/SCO mics.\n";
    out << "# on-demand = always-safe; ~1 s of zero padding on first PCM after F2.\n";
    out << "CaptureMode = " << captureModeToString(captureMode) << "\n";

    // Group backendOptions by section.
    QHash<QString, QVariantHash> bySection;
    for (auto it = backendOptions.constBegin(); it != backendOptions.constEnd(); ++it) {
        const QString full = it.key();
        const int slash = full.indexOf(QLatin1Char('/'));
        if (slash <= 0) continue;
        bySection[full.left(slash)].insert(full.mid(slash + 1), it.value());
    }
    auto sections = bySection.keys();
    std::sort(sections.begin(), sections.end());
    for (const auto &section : sections) {
        out << "\n[" << section << "]\n";
        const auto &kv = bySection[section];
        auto keys = kv.keys();
        std::sort(keys.begin(), keys.end());
        for (const auto &k : keys) {
            out << k << " = " << kv.value(k).toString() << "\n";
        }
    }
    return f.commit();
}
