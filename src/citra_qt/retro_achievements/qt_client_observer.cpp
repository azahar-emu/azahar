// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "qt_client_observer.h"

#include "common/logging/log.h"

namespace RetroAchievements {

void QtClientObserver::OnLoginSucceeded(const rc_client_user_t* user) {
    emit LoginSucceeded(user);
}

void QtClientObserver::OnLoginFailed(int result, const char* error_message) {
    emit LoginFailed(error_message);
}

void QtClientObserver::OnLoadGameSucceeded(const rc_client_game_t* game) {
    emit LoadGameSucceeded(game);
}

void QtClientObserver::OnLoadGameFailed(int result, const char* error_message) {
    emit LoadGameFailed(error_message);
}

} // namespace RetroAchievements
