#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr const char *kVolcengine = "volcengine";
} // namespace

SettingsDialog::SettingsDialog(OverlayConfig cfg, QWidget *parent)
    : QDialog(parent), cfg_(std::move(cfg)) {
    setWindowTitle(QStringLiteral("AnyTalk 配置"));
    // Force a normal centered dialog look. Without these the compositor
    // (sway/Hyprland) sometimes opens the dialog tiled or full-bleed
    // because no parent window exists in the --settings CLI path.
    setWindowFlag(Qt::Dialog, true);
    setWindowModality(Qt::ApplicationModal);
    setSizeGripEnabled(false);
    setMinimumWidth(420);
    setMaximumWidth(640);

    auto *root = new QVBoxLayout(this);

    auto *intro = new QLabel(
        QStringLiteral("<i>填写 ASR 后端凭据。配置保存到 %1</i>")
            .arg(OverlayConfig::configFilePath()),
        this);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("color: gray; padding-bottom: 6px;"));
    root->addWidget(intro);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    backendCombo_ = new QComboBox(this);
    backendCombo_->addItem(QStringLiteral("Volcengine 豆包 (中文)"), QString::fromLatin1(kVolcengine));
    // Future backends register here.
    const int idx = backendCombo_->findData(cfg_.backend);
    backendCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
    form->addRow(QStringLiteral("ASR 后端"), backendCombo_);

    appIdEdit_ = new QLineEdit(
        cfg_.str(QStringLiteral("Volcengine"), QStringLiteral("AppID")), this);
    appIdEdit_->setPlaceholderText(QStringLiteral("Volcengine App ID"));
    form->addRow(QStringLiteral("App ID"), appIdEdit_);

    tokenEdit_ = new QLineEdit(
        cfg_.str(QStringLiteral("Volcengine"), QStringLiteral("AccessToken")), this);
    tokenEdit_->setPlaceholderText(QStringLiteral("Access Token"));
    tokenEdit_->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    form->addRow(QStringLiteral("Access Token"), tokenEdit_);

    // Combo data is a synthetic key combining mode + two-pass; onAccept
    // splits it back into the two conf fields (Volcengine/Mode and
    // Volcengine/EnableNonstream). Keeping it as one picker is clearer
    // for users than mode + a separate "enable two-pass" checkbox whose
    // state is meaningless when mode != bidi.
    volcModeCombo_ = new QComboBox(this);
    volcModeCombo_->addItem(QStringLiteral("双向流式 + 二遍识别 — 又快又准 ★ 推荐"),
                             QStringLiteral("bidi_2pass"));
    volcModeCombo_->addItem(QStringLiteral("双向异步流式 — 实时"),
                             QStringLiteral("bidi_async"));
    volcModeCombo_->addItem(QStringLiteral("双向流式 — 最快"),
                             QStringLiteral("bidi"));
    volcModeCombo_->addItem(QStringLiteral("流式输入 — 仅出最终结果, 最准"),
                             QStringLiteral("nostream"));
    {
        const QString rawMode = cfg_.str(QStringLiteral("Volcengine"),
                                          QStringLiteral("Mode"),
                                          QStringLiteral("bidi_async"));
        const bool nonstream = cfg_.boolean(QStringLiteral("Volcengine"),
                                             QStringLiteral("EnableNonstream"), false);
        const QString cur = (rawMode == QLatin1String("bidi") && nonstream)
                                ? QStringLiteral("bidi_2pass")
                                : rawMode;
        const int vmIdx = volcModeCombo_->findData(cur);
        volcModeCombo_->setCurrentIndex(vmIdx >= 0 ? vmIdx : 0);
    }
    form->addRow(QStringLiteral("识别模式"), volcModeCombo_);

    trimCheck_ = new QCheckBox(QStringLiteral("自动去除句末标点"), this);
    trimCheck_->setChecked(cfg_.removeTrailingPunctuation);
    form->addRow(QString(), trimCheck_);

    root->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

void SettingsDialog::onAccept() {
    cfg_.backend = backendCombo_->currentData().toString();
    cfg_.removeTrailingPunctuation = trimCheck_->isChecked();
    cfg_.backendOptions.insert(QStringLiteral("Volcengine/AppID"), appIdEdit_->text().trimmed());
    cfg_.backendOptions.insert(QStringLiteral("Volcengine/AccessToken"), tokenEdit_->text().trimmed());
    {
        const QString picked = volcModeCombo_->currentData().toString();
        const bool twoPass = (picked == QLatin1String("bidi_2pass"));
        const QString modeStr = twoPass ? QStringLiteral("bidi") : picked;
        cfg_.backendOptions.insert(QStringLiteral("Volcengine/Mode"), modeStr);
        cfg_.backendOptions.insert(QStringLiteral("Volcengine/EnableNonstream"),
                                    twoPass ? QStringLiteral("True") : QStringLiteral("False"));
    }

    if (!cfg_.isUsable()) {
        QMessageBox::warning(this, QStringLiteral("缺少凭据"),
                              QStringLiteral("请填写 App ID 和 Access Token。"));
        return;
    }

    if (!cfg_.save()) {
        QMessageBox::critical(this, QStringLiteral("保存失败"),
                               QStringLiteral("无法写入 %1").arg(OverlayConfig::configFilePath()));
        return;
    }
    accept();
}
