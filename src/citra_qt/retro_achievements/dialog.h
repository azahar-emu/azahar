// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <optional>

#include <QDialog>
#include <QPixmap>
#include <QString>

#include "retro_achievements/models.h"

namespace Ui {
class RetroAchievementsDialog;
}

namespace RetroAchievements {

class Dialog : public QDialog {
    Q_OBJECT

public:
    explicit Dialog(const std::optional<User>& user, bool enabled, QWidget* parent = 0);
    ~Dialog() override;

private slots:
    void OnAuthenticationButtonPressed();
    void OnEnabledToggled(bool enabled);

public slots:
    void OnLoginSucceeded(const User& user);
    void OnLoginFailed(const QString& error_message);

    void OnAvatarImageDownloaded(QPixmap image);

signals:
    void EnabledToggled(bool enabled);
    void LogInAttempted(const QString& username, const QString& password);
    void LoggedOut();

private:
    std::unique_ptr<Ui::RetroAchievementsDialog> ui;

    std::optional<User> m_user;
    QString m_error_message;

    void UpdateUI();
};

} // namespace RetroAchievements
