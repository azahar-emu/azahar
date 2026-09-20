// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <type_traits>
#include <utility>
#include "common/common_types.h"

namespace Common {

#if defined(_MSC_VER) && !defined(__clang__)
#define UNROLL_ATTRS [[msvc::forceinline]]
#define UNROLL_IMPL_ATTRS [[msvc::forceinline]]
#define UNROLL_FORCE_INLINE_CALLS [[msvc::forceinline_calls]]
#else
#define UNROLL_ATTRS [[gnu::always_inline]]
#define UNROLL_IMPL_ATTRS [[gnu::always_inline, gnu::flatten]]
#define UNROLL_FORCE_INLINE_CALLS
#endif

template <typename F, u32... Is>
UNROLL_IMPL_ATTRS inline void unroll_impl(F&& f, std::integer_sequence<u32, Is...>) {
    UNROLL_FORCE_INLINE_CALLS(f(std::integral_constant<u32, Is>{}), ...);
}

template <u32 N, typename F>
UNROLL_ATTRS inline void unroll(F&& f) {
    unroll_impl(f, std::make_integer_sequence<u32, N>{});
}

#undef UNROLL_ATTRS
#undef UNROLL_IMPL_ATTRS
#undef UNROLL_FORCE_INLINE_CALLS

} // namespace Common