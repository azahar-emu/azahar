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

    connect(ui->authentication_button, &QPushButton::clicked, this,
            &Dialog::OnAuthenticationButtonPressed);
    connect(ui->enabled_check_box, &QCheckBox::toggled, this, &Dialog::UpdateUI);

    UpdateUI();
}

Dialog::~Dialog() = default;

void Dialog::OnAuthenticationButtonPressed() {
    LOG_DEBUG(Frontend, "Dialog::OnAuthenticationButtonPressed");

    if (m_user) {
        m_user = nullptr;
        ui->password_input->clear();
    } else {
        QString username = ui->username_input->text();
        QString password = ui->password_input->text();

        if (username.isEmpty() || password.isEmpty()) {
            m_error_message = "Fields cannot be empty.";
        } else {
            emit LogInAttempted(username, password);
            return;
        }
    }

    UpdateUI();
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

    m_user = user;
    m_error_message = nullptr;

    UpdateUI();
}

void Dialog::OnLoginFailed(const char* error_message) {
    LOG_DEBUG(Frontend, "Dialog::OnLoginFailed");

    m_error_message = error_message;

    UpdateUI();
}

void Dialog::UpdateUI() {
    bool is_enabled = ui->enabled_check_box->isChecked();
    bool has_user = m_user != nullptr;

    ui->user_display->setVisible(is_enabled && has_user);

    ui->password_label->setVisible(!has_user);
    ui->password_input->setVisible(!has_user);

    ui->authentication_credentials->setEnabled(is_enabled && !has_user);

    if (m_user) {
        ui->user_display_name->setText(QString::fromUtf8(m_user->username));
        ui->user_display_points->setText(QStringLiteral("%1 points").arg(m_user->score));
        // The avatar image is set in `OnAvatarImageDownloaded`.
    }

    ui->authentication_error_label->setVisible(m_error_message != nullptr);
    if (m_error_message) {
        ui->authentication_error_label->setText(QString::fromUtf8(m_error_message));
    }

    ui->authentication_button->setEnabled(is_enabled);
    ui->authentication_button->setText(has_user ? QStringLiteral("Log Out")
                                                : QStringLiteral("Log In"));
}

} // namespace RetroAchievements
