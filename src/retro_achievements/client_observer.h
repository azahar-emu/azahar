// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <rc_client.h>

namespace RetroAchievements {

class ClientObserver {
public:
    virtual ~ClientObserver() = default;

    virtual void OnLoginSucceeded(const rc_client_user_t* user) = 0;
    virtual void OnLoginFailed(int result, const char* error_message) = 0;
};

} // namespace RetroAchievements