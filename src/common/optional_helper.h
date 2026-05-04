// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <optional>
#include <type_traits>

namespace detail {
template <typename T>
struct is_optional_trait : std::false_type {
    using value_type = T;
};

template <typename T>
struct is_optional_trait<std::optional<T>> : std::true_type {
    using value_type = T;
};
} // namespace detail

/**
 * Returns true if T is a std::optional, false otherwise.
 * For example:
 * using Test1 = u32;
 * using Test2 = std::optional<u32>;
 * is_optional_type<Test1> -> false
 * is_optional_type<Test2> -> true
 */
template <typename T>
inline constexpr bool is_optional_type = detail::is_optional_trait<T>::value;

/**
 * Provides the inner type of T if it is a std::optional, or T itself if it is not.
 * For example:
 * using Test1 = u32;
 * using Test2 = std::optional<u32>;
 * optional_value_type<Test1> -> u32
 * optional_value_type<Test2> -> u32
 */
template <typename T>
using optional_inner_or_type = typename detail::is_optional_trait<T>::value_type;
