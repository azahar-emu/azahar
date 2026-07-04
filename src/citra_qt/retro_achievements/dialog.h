// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QDialog>
#include <QPixmap>
#include <QString>
#include <rc_client.h>

namespace Ui {
class RetroAchievementsDialog;
}

namespace RetroAchievements {

class Dialog : public QDialog {
    Q_OBJECT

public:
    explicit Dialog(bool enabled, QWidget* parent = 0);
    ~Dialog() override;

private slots:
    void OnAuthenticationButtonPressed();
    void OnEnabledToggled(bool enabled);

public slots:
    void OnLoginSucceeded(const rc_client_user_t* user);
    void OnLoginFailed(const char* error_message);

    void OnAvatarImageDownloaded(QPixmap image);

signals:
    void EnabledToggled(bool enabled);
    void LogInAttempted(const QString& username, const QString& password);
    void LoggedOut();

private:
    std::unique_ptr<Ui::RetroAchievementsDialog> ui;

    const rc_client_user_t* m_user = nullptr;
    const char* m_error_message = nullptr;

    void UpdateUI();
};

} // namespace RetroAchievements
