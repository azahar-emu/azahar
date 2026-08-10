// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstdint>
#include <string>

namespace RetroAchievements {

struct User {
    std::string display_name;
    std::string username;
    std::string token;
    uint32_t score;
    std::string avatar_url;
};

struct Game {
    std::string title;
    std::string badge_url;
};

} // namespace RetroAchievements
