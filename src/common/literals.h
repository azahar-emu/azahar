// Copyright 2021 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include "common/common_types.h"

namespace Common::Literals {

constexpr u64 operator""_KiB(unsigned long long int x) {
    return 1024ULL * x;
}

constexpr u64 operator""_MiB(unsigned long long int x) {
    return 1024_KiB * x;
}

constexpr u64 operator""_GiB(unsigned long long int x) {
    return 1024_MiB * x;
}

constexpr u64 operator""_TiB(unsigned long long int x) {
    return 1024_GiB * x;
}

constexpr u64 operator""_PiB(unsigned long long int x) {
    return 1024_TiB * x;
}

} // namespace Common::Literals
