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
    explicit Dialog(QWidget* parent = 0);
    ~Dialog() override;

private slots:
    void OnLoginButtonPressed();
    void OnEnabledStateChanged(bool checked);

public slots:
    void OnLoginSucceeded(const rc_client_user_t* user);
    void OnLoginFailed();

    void OnAvatarImageDownloaded(QPixmap image);

signals:
    void Toggled(bool enabled);
    void LogInAttempted(const QString& username, const QString& password);
    void LoggedOut();

private:
    std::unique_ptr<Ui::RetroAchievementsDialog> ui;
};

} // namespace RetroAchievements
