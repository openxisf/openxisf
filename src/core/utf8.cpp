// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "core/utf8.h"

#include <cstddef>

namespace openxisf::detail {

bool is_valid_utf8(std::string_view text) noexcept
{
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80U) {
            if (lead == 0) {
                return false;
            }
            ++i;
            continue;
        }

        // The well-formed sequences of table 3-7. The lead byte gives the length, and restricts the range of the
        // second byte to exclude overlong forms (E0, F0), surrogates (ED) and code points above U+10FFFF (F4).
        std::size_t length = 0;
        unsigned char low = 0x80U;
        unsigned char high = 0xBFU;
        if (lead >= 0xC2U && lead <= 0xDFU) {
            length = 2;
        } else if (lead == 0xE0U) {
            length = 3;
            low = 0xA0U;
        } else if (lead == 0xEDU) {
            length = 3;
            high = 0x9FU;
        } else if (lead >= 0xE1U && lead <= 0xEFU) {
            length = 3;
        } else if (lead == 0xF0U) {
            length = 4;
            low = 0x90U;
        } else if (lead == 0xF4U) {
            length = 4;
            high = 0x8FU;
        } else if (lead >= 0xF1U && lead <= 0xF3U) {
            length = 4;
        } else {
            // A continuation byte, an overlong lead byte (C0, C1) or a byte that never occurs (F5 to FF).
            return false;
        }

        if (text.size() - i < length) {
            return false;
        }
        const auto second = static_cast<unsigned char>(text[i + 1]);
        if (second < low || second > high) {
            return false;
        }
        for (std::size_t k = 2; k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if (next < 0x80U || next > 0xBFU) {
                return false;
            }
        }
        i += length;
    }
    return true;
}

} // namespace openxisf::detail
