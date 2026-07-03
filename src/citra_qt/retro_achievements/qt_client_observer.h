// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QObject>

#include "retro_achievements/client_observer.h"

namespace RetroAchievements {
class QtClientObserver : public QObject, public RetroAchievements::ClientObserver {
    Q_OBJECT

public:
    void OnLoginSucceeded(const rc_client_user_t* user) override;
    void OnLoginFailed(int result, const char* error_message);

signals:
    void LoginSucceeded(const rc_client_user_t* user);
};
} // namespace RetroAchievements
