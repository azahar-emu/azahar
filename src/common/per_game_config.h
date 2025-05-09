#pragma once

#include <unordered_map>
#include "common/settings.h"

namespace PerGameConfig {

// Map of title IDs to their default settings
const std::unordered_map<u64, std::unordered_map<std::string, u16>> default_settings = {
    // Title ID 00040000000CFF00
    {0x00040000000CFF00, {
        {"delay_game_render_thread_us", 9500}
    }},
    // Title ID 0004000000055F00
    {0x0004000000055F00, {
        {"delay_game_render_thread_us", 9500}
    }},
    // Title ID 0004000000076500
    {0x0004000000076500, {
        {"delay_game_render_thread_us", 9500}
    }},
    // Title ID 00040000000D0000
    {0x00040000000D0000, {
        {"delay_game_render_thread_us", 9500}
    }}
};

// Function to get default settings for a title ID
inline const std::unordered_map<std::string, u16>& GetDefaultSettings(u64 title_id) {
    static const std::unordered_map<std::string, u16> empty;
    auto it = default_settings.find(title_id);
    return it != default_settings.end() ? it->second : empty;
}

} / namespace PerGameConfig