#pragma once
#include <QColor>
#include <QString>

namespace Theme {

// Aurora hue (matches voice-overlay.jsx variant="aurora", hue=170 → mint/cyan).
inline constexpr int AURORA_HUE = 170;

// Bottom dock layout — compact horizontal pill anchored to bottom of screen.
inline constexpr int BAR_COUNT = 24;
inline constexpr int BAR_AREA_WIDTH = 200;
inline constexpr int BAR_AREA_HEIGHT = 32;

inline constexpr int CARD_RADIUS = 28;
inline constexpr int CARD_PAD_X = 18;
inline constexpr int CARD_PAD_Y = 12;
inline constexpr int CARD_MIN_WIDTH = 380;
inline constexpr int CARD_BOTTOM_MARGIN = 80; // distance from screen bottom

// Transcript: wraps to multiple lines when it would exceed
// TRANSCRIPT_WIDTH_FRACTION of the primary screen's width. Beyond
// TRANSCRIPT_MAX_LINES rows we drop the front of the text and prepend "…",
// keeping the latest content visible. Full utterance still goes to the
// focus input field via CommitText.
inline constexpr double TRANSCRIPT_WIDTH_FRACTION = 2.0 / 3.0;
inline constexpr int TRANSCRIPT_MIN_WIDTH = 240;
inline constexpr int TRANSCRIPT_MAX_LINES = 3;

inline constexpr int STATUS_DOT_DIAMETER = 10;
inline constexpr int FADE_MS = 200;

// Approximate sRGB renditions of the JS prototype's oklch palette (hue=170).
inline QColor accent() { return QColor("#7ee7d3"); }
inline QColor accentDeep() { return QColor("#3fb39e"); }
inline QColor accentDark() { return QColor("#1f5c52"); }

inline QColor errorColor() { return QColor("#ff7a85"); }
inline QColor connectingColor() { return QColor("#ffc56b"); }

inline QColor cardBg() { return QColor(14, 16, 22, 215); }
inline QColor cardStroke() { return QColor(255, 255, 255, 26); }
inline QColor textPrimary() { return QColor(255, 255, 255, 242); }
inline QColor textDim() { return QColor(255, 255, 255, 140); }
inline QColor textPlaceholder() { return QColor(255, 255, 255, 110); }
inline QColor hintColor() { return QColor(255, 255, 255, 110); }

} // namespace Theme
