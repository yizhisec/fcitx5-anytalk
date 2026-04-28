#pragma once
#include "Config.h"
#include <QDialog>

class QComboBox;
class QLineEdit;
class QCheckBox;

/// Minimal settings UI. The first version covers only the fields needed to
/// start recording with the Volcengine backend. Future backends will surface
/// additional sections; a richer multi-tab dialog is planned but not built
/// here yet — keep this small and obvious.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(OverlayConfig cfg, QWidget *parent = nullptr);

    /// The (potentially modified) config to persist. Valid after exec()
    /// returns QDialog::Accepted.
    OverlayConfig config() const { return cfg_; }

private slots:
    void onAccept();

private:
    OverlayConfig cfg_;
    QComboBox *backendCombo_ = nullptr;
    QLineEdit *appIdEdit_ = nullptr;
    QLineEdit *tokenEdit_ = nullptr;
    QCheckBox *trimCheck_ = nullptr;
};
