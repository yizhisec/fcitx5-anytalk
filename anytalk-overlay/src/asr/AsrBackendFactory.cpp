#include "AsrBackendFactory.h"
#include "Config.h"
#include "VolcengineBackend.h"

#include <QDebug>

namespace asr {

std::unique_ptr<AsrBackend> create(const OverlayConfig &cfg, QObject *parent) {
    if (cfg.backend == QLatin1String("volcengine")) {
        VolcengineBackend::Settings s;
        s.appId = cfg.str(QStringLiteral("Volcengine"), QStringLiteral("AppID"));
        s.accessToken = cfg.str(QStringLiteral("Volcengine"), QStringLiteral("AccessToken"));
        const auto resourceId = cfg.str(QStringLiteral("Volcengine"),
                                         QStringLiteral("ResourceId"));
        if (!resourceId.isEmpty()) s.resourceId = resourceId;
        const auto mode = cfg.str(QStringLiteral("Volcengine"), QStringLiteral("Mode"));
        if (!mode.isEmpty()) s.mode = mode;

        if (s.appId.isEmpty() || s.accessToken.isEmpty()) {
            qWarning() << "asr::create: Volcengine credentials missing — open SettingsDialog.";
            return nullptr;
        }
        auto backend = std::make_unique<VolcengineBackend>(s, parent);
        return backend;
    }
    qWarning() << "asr::create: unknown backend" << cfg.backend;
    return nullptr;
}

} // namespace asr
