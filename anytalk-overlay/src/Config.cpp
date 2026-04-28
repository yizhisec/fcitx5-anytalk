#include "Config.h"
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

OverlayConfig OverlayConfig::load() {
    OverlayConfig cfg;
    const QString path = QDir::homePath() +
                         QStringLiteral("/.config/fcitx5/conf/anytalk.conf");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return cfg;
    }
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString raw = in.readLine().trimmed();
        if (raw.isEmpty() || raw.startsWith('[') || raw.startsWith('#')) continue;
        const int eq = raw.indexOf('=');
        if (eq <= 0) continue;
        const QString key = raw.left(eq).trimmed();
        const QString val = raw.mid(eq + 1).trimmed();
        if (key == QLatin1String("AppID")) cfg.appId = val;
        else if (key == QLatin1String("AccessToken")) cfg.accessToken = val;
        else if (key == QLatin1String("RemoveTrailingPunctuation"))
            cfg.removeTrailingPunctuation = (val == QLatin1String("True") ||
                                              val == QLatin1String("true") ||
                                              val == QStringLiteral("1"));
    }
    return cfg;
}
