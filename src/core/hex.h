// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Hexadecimal digits. The formats of XISF write the lowercase ones: Base16 data, digests, UUIDs, index identifiers.

namespace openxisf::detail {

/// The digits of base 16, lowercase.
inline constexpr std::string_view hex_digits = "0123456789abcdef";

/// The value of a hexadecimal digit in either case, which is also its value in any base up to 16, or 16 for any other
/// character.
[[nodiscard]] constexpr unsigned hex_digit_value(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned>(c - 'a') + 10U;
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<unsigned>(c - 'A') + 10U;
    }
    return 16;
}

/// The 16 hexadecimal digits of value, leading zeros included.
[[nodiscard]] inline std::string fixed_width_hex(std::uint64_t value)
{
    std::string text;
    for (int shift = 60; shift >= 0; shift -= 4) {
        text += hex_digits[(value >> static_cast<unsigned>(shift)) & 0xFU];
    }
    return text;
}

} // namespace openxisf::detail
