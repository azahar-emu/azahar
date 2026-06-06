// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>

#include "common/common_types.h"
#include "core/loader/loader.h"

namespace FileSys::Patch {

Loader::ResultStatus ApplyIpsPatch(const std::vector<u8>& patch, std::vector<u8>& buffer);

Loader::ResultStatus ApplyBpsPatch(const std::vector<u8>& patch, std::vector<u8>& buffer);

} // namespace FileSys::Patch
