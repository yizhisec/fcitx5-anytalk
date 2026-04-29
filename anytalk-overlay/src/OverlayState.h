#pragma once
#include <QString>

/// Single source of truth for the state machine shared between
/// AsrController, OverlayWindow, and the D-Bus surface.
///
/// Two representations live here:
///   - `state::State` — type-safe enum used internally by AsrController.
///     Use this for `if (currentState_ == state::State::Idle)` style logic.
///   - `state::Idle / Connecting / Recording / Error` — QString constants
///     used on signals and D-Bus to keep wire compatibility with existing
///     subscribers (waybar custom modules, the fcitx5 addon).
///
/// Convert enum → string at signal-emission time via `state::toString()`.
namespace state {

enum class State { Idle, Connecting, Recording, Error };

inline const QString Idle       = QStringLiteral("idle");
inline const QString Connecting = QStringLiteral("connecting");
inline const QString Recording  = QStringLiteral("recording");
inline const QString Error      = QStringLiteral("error");

inline const QString &toString(State s) {
    switch (s) {
    case State::Idle:       return Idle;
    case State::Connecting: return Connecting;
    case State::Recording:  return Recording;
    case State::Error:      return Error;
    }
    return Idle; // unreachable; silences -Wreturn-type
}

} // namespace state
