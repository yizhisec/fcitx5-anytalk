#pragma once

namespace Constants {
    // Connection states
    inline constexpr const char* STATE_IDLE = "idle";
    inline constexpr const char* STATE_CONNECTED = "connected";
    inline constexpr const char* STATE_CONNECTING = "connecting";
    inline constexpr const char* STATE_RECORDING = "recording";

    // Message types
    inline constexpr const char* MSG_TYPE_START = "start";
    inline constexpr const char* MSG_TYPE_STOP = "stop";
    inline constexpr const char* MSG_TYPE_CANCEL = "cancel";
    inline constexpr const char* MSG_TYPE_PARTIAL = "partial";
    inline constexpr const char* MSG_TYPE_FINAL = "final";
    inline constexpr const char* MSG_TYPE_STATUS = "status";

    // JSON keys
    inline constexpr const char* JSON_KEY_TYPE = "type";
    inline constexpr const char* JSON_KEY_TEXT = "text";
    inline constexpr const char* JSON_KEY_STATE = "state";
    inline constexpr const char* JSON_KEY_MODE = "mode";
    inline constexpr const char* JSON_KEY_TOGGLE = "toggle";

    // UI labels
    inline constexpr const char* LABEL_RECORDING = "REC";
    inline constexpr const char* LABEL_CONNECTING = "...";
    inline constexpr const char* LABEL_READY = "RDY";
    inline constexpr const char* LABEL_DEFAULT = "AT";

    // Icons
    inline constexpr const char* ICON_RECORDING = "media-record";
    inline constexpr const char* ICON_DEFAULT = "anytalk";
}
