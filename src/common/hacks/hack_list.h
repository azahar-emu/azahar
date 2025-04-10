// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

namespace Common::Hacks {

enum class HackType : int {
    RIGHT_EYE_DISABLE,
    ACCURATE_MULTIPLICATION,
    DECRYPTION_AUTHORIZED,
    ONLINE_LLE_REQUIRED,
    REGION_FROM_SECURE,
};

class UserHackData {};

} // namespace Common::Hacks