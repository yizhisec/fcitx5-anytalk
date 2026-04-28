#pragma once
#include <QString>

struct OverlayConfig {
    QString appId;
    QString accessToken;
    bool removeTrailingPunctuation = false;

    /// Loads from $HOME/.config/fcitx5/conf/anytalk.conf — same file the
    /// fcitx5 anytalk addon historically wrote, so existing user
    /// configurations keep working.
    static OverlayConfig load();
};
