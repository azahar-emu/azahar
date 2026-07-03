// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <rc_client.h>

namespace RetroAchievements {

class Client {
public:
    explicit Client();
    ~Client();

    void AttemptLogin(const char* username, const char* password);

private:
    rc_client_t* m_rc_client;
};

} // namespace RetroAchievements