// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string_view>

#include <QObject>
#include <QString>

#include "retro_achievements/client_observer.h"

namespace RetroAchievements {

class QtClientObserver : public QObject, public RetroAchievements::ClientObserver {
    Q_OBJECT

public:
    void OnLoginSucceeded(const rc_client_user_t* user) override;
    void OnLoginFailed(int result, std::string_view error_message) override;

    void OnLoadGameSucceeded(const rc_client_game_t* game) override;
    void OnLoadGameFailed(int result, std::string_view error_message) override;
    void OnEvent(const rc_client_event_t* event) override;

signals:
    void LoginSucceeded(const rc_client_user_t* user);
    void LoginFailed(const QString& error_message);

    void LoadGameSucceeded(const rc_client_game_t* game);
    void LoadGameFailed(const QString& error_message);

    void EventNotification(const QString& title, const QString& body, const QString& image_url);
};

} // namespace RetroAchievements
