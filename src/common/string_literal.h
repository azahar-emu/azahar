// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <algorithm>
#include <cstddef>

namespace Common {

template <std::size_t N>
struct StringLiteral {
    constexpr StringLiteral(const char (&str)[N]) {
        std::copy_n(str, N, value);
    }

    static constexpr std::size_t strlen = N - 1;
    static constexpr std::size_t size = N;

    char value[N];
};

} // namespace Common
