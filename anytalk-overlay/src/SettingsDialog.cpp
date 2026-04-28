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
    setMinimumWidth(420);

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
