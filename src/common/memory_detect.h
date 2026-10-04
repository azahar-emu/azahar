// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include "common/common_types.h"

namespace Common {

struct MemoryInfo {
    u64 total_physical_memory{};
    u64 total_swap_memory{};
};

/**
 * Gets the memory info of the host system
 * @return Reference to a MemoryInfo struct with the physical and swap memory sizes in bytes
 */
[[nodiscard]] const MemoryInfo GetMemInfo();

/**
 * Gets the page size of the host system
 * @return Page size in bytes of the host system
 */
u64 GetPageSize();

} // namespace Common
