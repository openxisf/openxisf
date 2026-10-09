// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/error.h>

#include <concepts>
#include <limits>
#include <utility>

// Size and offset computations on values read from a unit, and in the layout that the writer computes. A result that
// does not fit means that the unit describes something that cannot exist, so an overflow is reported as invalid data,
// by the writer too: a model whose layout overflows 64 bits cannot be written.

namespace openxisf::detail {

[[noreturn]] void throw_arithmetic_overflow();

/// Unsigned types that integer promotion leaves unchanged, so that arithmetic on them stays in the type: the types of
/// sizes and offsets.
template <typename T>
concept wide_unsigned = std::unsigned_integral<T> && sizeof(T) >= sizeof(unsigned int);

/// a + b, or invalid_data_error when the sum does not fit in T.
template <wide_unsigned T> [[nodiscard]] constexpr T checked_add(T a, T b)
{
    if (b > std::numeric_limits<T>::max() - a) {
        throw_arithmetic_overflow();
    }
    return a + b;
}

/// a × b, or invalid_data_error when the product does not fit in T.
template <wide_unsigned T> [[nodiscard]] constexpr T checked_multiply(T a, T b)
{
    if (a != 0 && b > std::numeric_limits<T>::max() / a) {
        throw_arithmetic_overflow();
    }
    return a * b;
}

/// value converted to To, or invalid_data_error when To cannot represent it.
template <std::integral To, std::integral From> [[nodiscard]] constexpr To checked_cast(From value)
{
    if constexpr (std::same_as<To, From>) {
        return value;
    } else {
        if (!std::in_range<To>(value)) {
            throw_arithmetic_overflow();
        }
        return static_cast<To>(value);
    }
}

} // namespace openxisf::detail
