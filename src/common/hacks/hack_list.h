// Copyright 2024-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include "common/common_types.h"

namespace Common::Hacks {

enum class HackType : int {
    RIGHT_EYE_DISABLE,
    ACCURATE_MULTIPLICATION,
    DECRYPTION_AUTHORIZED,
    ONLINE_LLE_REQUIRED,
    REGION_FROM_SECURE,
    REQUIRES_SHADER_FIXUP,
    SPOOF_FRIEND_CODE_SEED,
    DELAY_TEXTURE_COPY_COMPLETION,
    SKIP_SLOW_DRAW,
    SKIP_TEXTURE_COPY,
    DISABLE_CLIP_COEF,
    SKIP_REDUNDANT_Y2R_CONVERSION,
};

class UserHackData {};

} // namespace Common::Hacks