// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include "hack_manager.h"

namespace Common::Hacks {
const HackEntry* HackManager::GetHack(const HackType& type, u64 title_id) {
    auto range = entries.equal_range(type);

    for (auto it = range.first; it != range.second; it++) {
        auto tid_found = std::find(it->second.affected_title_ids.begin(),
                                   it->second.affected_title_ids.end(), title_id);
        if (tid_found != it->second.affected_title_ids.end()) {
            return &it->second;
        }
    }

    return nullptr;
}
} // namespace Common::Hacks
