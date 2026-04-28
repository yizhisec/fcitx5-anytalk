#pragma once
#include <QString>

/// Single source of truth for the state-machine strings shared between
/// AsrController, OverlayWindow, and the D-Bus surface. The matching
/// fcitx5 addon (src/constants.h) carries the same strings as `const char*`
/// — keep the two in sync if either changes.
namespace state {

inline const QString Idle       = QStringLiteral("idle");
inline const QString Connecting = QStringLiteral("connecting");
inline const QString Recording  = QStringLiteral("recording");
inline const QString Error      = QStringLiteral("error");

} // namespace state
