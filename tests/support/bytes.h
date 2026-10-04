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

} // namespace openxisf::test
