// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <vector>
#include "common/common_types.h"
#include "common/hacks/hack_list.h"

namespace Common::Hacks {

enum class HackAllowMode {
    ALLOW,
    DISALLOW,
    FORCE,
};

struct HackEntry {
    HackAllowMode mode{};
    std::vector<u64> affected_title_ids{};
    UserHackData* hack_data{nullptr};
};

struct HackManager {
    const HackEntry* GetHack(const HackType& type, u64 title_id);

    HackAllowMode GetHackAllowMode(const HackType& type, u64 title_id,
                                   HackAllowMode default_mode = HackAllowMode::ALLOW) {
        const HackEntry* hack = GetHack(type, title_id);
        return (hack != nullptr) ? hack->mode : default_mode;
    }

    /**
     * Overrides the provided boolean setting depending on the hack type for the title ID
     * If there is no hack, or the hack is set to allow, the setting value is returned
     * If the hack disallows, false is returned.
     * If the hack forces, true is returned.
     */
    bool OverrideBooleanSetting(const HackType& type, u64 title_id, bool setting_value) {
        const HackEntry* hack = GetHack(type, title_id);
        if (hack == nullptr)
            return setting_value;
        switch (hack->mode) {
        case HackAllowMode::DISALLOW:
            return false;
        case HackAllowMode::FORCE:
            return true;
        case HackAllowMode::ALLOW:
        default:
            break;
        }
        return setting_value;
    }

    std::multimap<HackType, HackEntry> entries;
};

extern HackManager hack_manager;

} // namespace Common::Hacks