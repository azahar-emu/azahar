// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "dialog.h"
#include "ui_dialog.h"

#include "common/logging/log.h"

namespace RetroAchievements {

Dialog::Dialog(QWidget* parent)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint),
      ui(std::make_unique<Ui::RetroAchievementsDialog>()) {
    ui->setupUi(this);

    ui->user_display->hide();
    ui->authentication_error_label->hide();

    connect(ui->authentication_button, &QPushButton::clicked, this, &Dialog::OnLoginButtonPressed);
    connect(ui->enabled_check_box, &QCheckBox::toggled, this, &Dialog::OnEnabledStateChanged);
}

Dialog::~Dialog() = default;

void Dialog::OnLoginButtonPressed() {
    LOG_DEBUG(Frontend, "Dialog::loginButtonPressed");

    QString username = ui->username_input->text();
    QString password = ui->password_input->text();

    if (username.isEmpty() || password.isEmpty()) {
        ui->authentication_error_label->setText(tr("Fields cannot be empty."));
        return;
    }

    emit LogInAttempted(username, password);
}

void Dialog::OnEnabledStateChanged(bool checked) {
    LOG_DEBUG(Frontend, "Dialog::enabledStateChanged");

    ui->authentication_credentials->setEnabled(checked);
}

void Dialog::OnAvatarImageDownloaded(QPixmap image) {
    LOG_DEBUG(Frontend, "Dialog::OnAvatarImageDownloaded");

    ui->user_display_avatar->setPixmap(
        image.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void Dialog::OnLoginSucceeded(const rc_client_user_t* user) {
    LOG_DEBUG(Frontend,
              "Dialog::OnLoginSucceeded(user[.display_name] = \"{}\", user[.avatar_url] = \"{}\")",
              user->display_name, user->avatar_url);

    ui->user_display_name->setText(QString::fromUtf8(user->username));
    ui->user_display_points->setText(ui->user_display_points->text().arg(user->score));

    ui->user_display->show();
}

void Dialog::OnLoginFailed() {
    LOG_DEBUG(Frontend, "STUB: Dialog::OnLoginFailed");
}

} // namespace RetroAchievements
