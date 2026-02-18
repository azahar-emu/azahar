// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.
#pragma once

#include "common/arch.h"
#if CITRA_ARCH(x86_64)

namespace Common {

/// x86/x64 CPU capabilities that may be detected by this module
struct CPUCaps {
    char cpu_string[0x21];
    char brand_string[0x41];
    bool sse;
    bool sse2;
    bool sse3;
    bool ssse3;
    bool sse4_1;
    bool sse4_2;
    bool avx;
    bool avx2;
    bool avx512;
    bool bmi1;
    bool bmi2;
    bool fma;
    bool aes;
};

/**
 * Gets the supported capabilities of the host CPU
 * @return Reference to a CPUCaps struct with the detected host CPU capabilities
 */
const CPUCaps& GetCPUCaps();

} // namespace Common

#endif // CITRA_ARCH(x86_64)
