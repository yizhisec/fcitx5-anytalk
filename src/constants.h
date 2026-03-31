#pragma once

namespace Constants {
    // Connection states
    inline constexpr const char* STATE_IDLE = "idle";
    inline constexpr const char* STATE_CONNECTED = "connected";
    inline constexpr const char* STATE_CONNECTING = "connecting";
    inline constexpr const char* STATE_RECORDING = "recording";

    // UI labels
    inline constexpr const char* LABEL_RECORDING = "REC";
    inline constexpr const char* LABEL_CONNECTING = "...";
    inline constexpr const char* LABEL_READY = "RDY";
    inline constexpr const char* LABEL_DEFAULT = "AT";

    // Icons
    inline constexpr const char* ICON_RECORDING = "media-record";
    inline constexpr const char* ICON_DEFAULT = "anytalk";

    // D-Bus
    inline constexpr const char* DBUS_SERVICE = "org.fcitx.Fcitx5.AnyTalk";
    inline constexpr const char* DBUS_PATH = "/anytalk";
    inline constexpr const char* DBUS_INTERFACE = "org.fcitx.Fcitx5.AnyTalk";
}
