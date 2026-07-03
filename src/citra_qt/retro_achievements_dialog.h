// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QDialog>
#include <QString>

namespace Ui {
class RetroAchievementsDialog;
}

class RetroAchievementsDialog : public QDialog {
    Q_OBJECT

public:
    explicit RetroAchievementsDialog(QWidget* parent = 0);
    ~RetroAchievementsDialog() override;

private slots:
    void onLoginButtonPressed();
    void onEnabledStateChanged(bool checked);

public slots:
    void onLoginSuccess();
    void onLoginError();

signals:
    void toggled(bool enabled);
    void logInAttempted(const QString& username, const QString& password);
    void loggedOut();

private:
    std::unique_ptr<Ui::RetroAchievementsDialog> ui;
};
