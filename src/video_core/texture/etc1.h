// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include "common/common_types.h"
#include "common/vector_math.h"

namespace Pica::Texture {

Common::Vec3<u8> SampleETC1Subtile(u64 value, unsigned int x, unsigned int y);

} // namespace Pica::Texture
