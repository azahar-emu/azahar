// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "retro_achievements_dialog.h"
#include "ui_retro_achievements_dialog.h"

#include "common/logging/log.h"

RetroAchievementsDialog::RetroAchievementsDialog(QWidget* parent)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint),
      ui(std::make_unique<Ui::RetroAchievementsDialog>()) {
    ui->setupUi(this);

    ui->authentication_error_label->hide();

    connect(ui->authentication_button, &QPushButton::clicked, this,
            &RetroAchievementsDialog::onLoginButtonPressed);
    connect(ui->enabled_check_box, &QCheckBox::toggled, this,
            &RetroAchievementsDialog::onEnabledStateChanged);
}

RetroAchievementsDialog::~RetroAchievementsDialog() = default;

void RetroAchievementsDialog::onLoginButtonPressed() {
    LOG_DEBUG(Frontend, "RetroAchievementsDialog::loginButtonPressed");

    QString username = ui->username_input->text();
    QString password = ui->password_input->text();

    if (username.isEmpty() || password.isEmpty()) {
        ui->authentication_error_label->setText(tr("Fields cannot be empty."));
        return;
    }

    emit logInAttempted(username, password);
}

void RetroAchievementsDialog::onEnabledStateChanged(bool checked) {
    LOG_DEBUG(Frontend, "RetroAchievementsDialog::enabledStateChanged");

    ui->authentication_credentials->setEnabled(checked);
}

void RetroAchievementsDialog::onLoginSuccess() {
    LOG_DEBUG(Frontend, "RetroAchievementsDialog::onLoginSuccess");
}

void RetroAchievementsDialog::onLoginError() {
    LOG_DEBUG(Frontend, "RetroAchievementsDialog::onLoginError");
}
