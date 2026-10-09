// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "core/quote.h"

#include "core/hex.h"

#include <cstddef>

namespace openxisf::detail {

std::string quote(std::string_view text)
{
    constexpr std::size_t max_length = 40;

    std::string quoted = "'";
    for (const char c : text.substr(0, max_length)) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte == '\'' || byte == '\\') {
            quoted += '\\';
            quoted += c;
        } else if (byte >= 0x20U && byte <= 0x7EU) {
            quoted += c;
        } else {
            quoted += "\\x";
            quoted += hex_digits[byte >> 4U];
            quoted += hex_digits[byte & 0x0FU];
        }
    }
    quoted += '\'';
    if (text.size() > max_length) {
        quoted += "...";
    }
    return quoted;
}

} // namespace openxisf::detail
