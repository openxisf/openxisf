// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openxisf::test {

/// The bytes of text.
[[nodiscard]] inline std::vector<std::byte> bytes(std::string_view text)
{
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const char c : text) {
        result.push_back(static_cast<std::byte>(c));
    }
    return result;
}

/// The text of bytes.
[[nodiscard]] inline std::string text(std::span<const std::byte> data)
{
    std::string result;
    result.reserve(data.size());
    for (const std::byte b : data) {
        result += static_cast<char>(b);
    }
    return result;
}

/// size bytes of a pattern with a period of 251, which is prime, so that a byte read from a wrong offset shows.
[[nodiscard]] inline std::vector<std::byte> pattern(std::size_t size)
{
    std::vector<std::byte> result(size);
    for (std::size_t i = 0; i < size; ++i) {
        result[i] = static_cast<std::byte>(i % 251);
    }
    return result;
}

/// The bytes of an ICC profile, whose content the library keeps without looking at it beyond its header: the pattern,
/// with the size of the profile in its first four bytes and the signature `acsp` at byte 36 (ICC.1:2022 §7.2). size is
/// at least 128 and below 2^32.
[[nodiscard]] inline std::vector<std::byte> icc_profile(std::size_t size = 132)
{
    std::vector<std::byte> profile = pattern(size);
    for (std::size_t i = 0; i < 4; ++i) {
        profile[i] = static_cast<std::byte>((size >> (8 * (3 - i))) & 0xFF);
    }
    const std::string_view signature = "acsp";
    for (std::size_t i = 0; i < signature.size(); ++i) {
        profile[36 + i] = static_cast<std::byte>(signature[i]);
    }
    return profile;
}

} // namespace openxisf::test
