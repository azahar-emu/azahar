// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string_view>

#include <rc_client.h>

namespace RetroAchievements {

class ClientObserver {
public:
    virtual ~ClientObserver() = default;

    virtual void OnLoginSucceeded(const rc_client_user_t* user) {}
    virtual void OnLoginFailed(int result, std::string_view error_message) {}

    virtual void OnLoadGameSucceeded(const rc_client_game_t* game) {}
    virtual void OnLoadGameFailed(int result, std::string_view error_message) {}

    virtual void OnEvent(const rc_client_event_t* event) {}
};

} // namespace RetroAchievements
