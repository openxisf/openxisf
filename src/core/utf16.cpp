// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "core/utf16.h"

#include <openxisf/error.h>

#include "core/quote.h"
#include "core/utf8.h"

#include <cstddef>
#include <cstdint>

namespace openxisf::detail {

namespace {

[[noreturn]] void throw_invalid_utf16(std::string_view reason, std::size_t position)
{
    throw invalid_data_error(errc::invalid_utf16, "invalid UTF-16 text: " + std::string(reason) + " at position " +
                                                      std::to_string(position));
}

void append_utf8(std::string& text, std::uint32_t code_point)
{
    const auto byte = [](std::uint32_t value) { return static_cast<char>(value); };
    if (code_point < 0x80U) {
        text += byte(code_point);
    } else if (code_point < 0x800U) {
        text += byte(0xC0U | (code_point >> 6U));
        text += byte(0x80U | (code_point & 0x3FU));
    } else if (code_point < 0x10000U) {
        text += byte(0xE0U | (code_point >> 12U));
        text += byte(0x80U | ((code_point >> 6U) & 0x3FU));
        text += byte(0x80U | (code_point & 0x3FU));
    } else {
        text += byte(0xF0U | (code_point >> 18U));
        text += byte(0x80U | ((code_point >> 12U) & 0x3FU));
        text += byte(0x80U | ((code_point >> 6U) & 0x3FU));
        text += byte(0x80U | (code_point & 0x3FU));
    }
}

} // namespace

std::u16string utf8_to_utf16(std::string_view text)
{
    if (!is_valid_utf8(text)) {
        throw invalid_data_error(errc::invalid_utf8, quote(text) + " is not valid UTF-8");
    }

    // The text is well-formed, so the lead byte gives the length of each sequence and no byte needs checking.
    std::u16string result;
    result.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::uint32_t code_point = lead;
        std::size_t length = 1;
        if (lead >= 0xF0U) {
            code_point = lead & 0x07U;
            length = 4;
        } else if (lead >= 0xE0U) {
            code_point = lead & 0x0FU;
            length = 3;
        } else if (lead >= 0xC0U) {
            code_point = lead & 0x1FU;
            length = 2;
        }
        for (std::size_t k = 1; k < length; ++k) {
            code_point = (code_point << 6U) | (static_cast<unsigned char>(text[i + k]) & 0x3FU);
        }
        i += length;

        if (code_point < 0x10000U) {
            result += static_cast<char16_t>(code_point);
        } else {
            const std::uint32_t offset = code_point - 0x10000U;
            result += static_cast<char16_t>(0xD800U + (offset >> 10U));
            result += static_cast<char16_t>(0xDC00U + (offset & 0x3FFU));
        }
    }
    return result;
}

std::string utf16_to_utf8(std::u16string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t code_point = text[i];
        if (code_point == 0) {
            throw_invalid_utf16("U+0000", i);
        }
        if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
            throw_invalid_utf16("a low surrogate without a high surrogate", i);
        }
        if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
            const std::uint32_t low = i + 1 < text.size() ? text[i + 1] : 0;
            if (low < 0xDC00U || low > 0xDFFFU) {
                throw_invalid_utf16("a high surrogate without a low surrogate", i);
            }
            code_point = 0x10000U + ((code_point - 0xD800U) << 10U) + (low - 0xDC00U);
            ++i;
        }
        append_utf8(result, code_point);
    }
    return result;
}

} // namespace openxisf::detail
