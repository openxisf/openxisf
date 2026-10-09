// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "core/data_encoding.h"

#include <openxisf/error.h>

#include "core/hex.h"
#include "core/quote.h"
#include "core/text_grammar.h"

#include <array>
#include <cstdint>

namespace openxisf::detail {

namespace {

constexpr std::string_view base64_alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr unsigned invalid_digit = 64;

// The value of a Base64 digit, or invalid_digit.
unsigned base64_value(char c) noexcept
{
    if (c >= 'A' && c <= 'Z') {
        return static_cast<unsigned>(c - 'A');
    }
    if (c >= 'a' && c <= 'z') {
        return static_cast<unsigned>(c - 'a') + 26U;
    }
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned>(c - '0') + 52U;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return invalid_digit;
}

// The value of a lowercase hexadecimal digit, or invalid_digit.
unsigned hex_value(char c) noexcept
{
    const unsigned value = hex_digit_value(c);
    return value == 16 || (c >= 'A' && c <= 'F') ? invalid_digit : value;
}

[[noreturn]] void throw_invalid_base64(std::string_view reason)
{
    throw invalid_data_error(errc::invalid_base64, "invalid Base64 data: " + std::string(reason));
}

[[noreturn]] void throw_invalid_character(errc code, std::string_view encoding, char c, std::size_t position)
{
    throw invalid_data_error(code, "invalid " + std::string(encoding) + " data: character " +
                                       quote(std::string_view(&c, 1)) + " at position " + std::to_string(position));
}

} // namespace

std::string encode_base64(std::span<const std::byte> data)
{
    std::string text;
    text.reserve((data.size() + 2) / 3 * 4);
    const auto append = [&text](std::uint32_t group, std::size_t digits) {
        for (std::size_t i = 0; i < 4; ++i) {
            text += i < digits ? base64_alphabet[(group >> (18 - (6 * i))) & 0x3FU] : '=';
        }
    };

    std::size_t i = 0;
    for (; data.size() - i >= 3; i += 3) {
        append((std::to_integer<std::uint32_t>(data[i]) << 16U) | (std::to_integer<std::uint32_t>(data[i + 1]) << 8U) |
                   std::to_integer<std::uint32_t>(data[i + 2]),
               4);
    }
    if (data.size() - i == 1) {
        append(std::to_integer<std::uint32_t>(data[i]) << 16U, 2);
    } else if (data.size() - i == 2) {
        append((std::to_integer<std::uint32_t>(data[i]) << 16U) | (std::to_integer<std::uint32_t>(data[i + 1]) << 8U),
               3);
    }
    return text;
}

std::vector<std::byte> decode_base64(std::string_view text)
{
    std::vector<std::byte> data;
    data.reserve(text.size() / 4 * 3);

    // Four digits make a group of 24 bits and three bytes.
    std::uint32_t group = 0;
    std::size_t digits = 0;
    std::size_t i = 0;
    for (; i < text.size() && text[i] != '='; ++i) {
        if (is_white_space(text[i])) {
            continue;
        }
        const unsigned value = base64_value(text[i]);
        if (value == invalid_digit) {
            throw_invalid_character(errc::invalid_base64, "Base64", text[i], i);
        }
        group = (group << 6U) | value;
        if (++digits == 4) {
            data.push_back(static_cast<std::byte>(group >> 16U));
            data.push_back(static_cast<std::byte>(group >> 8U));
            data.push_back(static_cast<std::byte>(group));
            group = 0;
            digits = 0;
        }
    }

    // A last group of two or three digits is completed by padding, and nothing but padding and white space follows.
    std::size_t padding = 0;
    for (; i < text.size(); ++i) {
        if (text[i] == '=') {
            ++padding;
        } else if (!is_white_space(text[i])) {
            throw_invalid_base64("data after the padding");
        }
    }
    if (digits + padding != 0 && (digits < 2 || digits + padding != 4)) {
        throw_invalid_base64("the length is not a multiple of four characters");
    }

    // The bits after the last byte must be zero, so that every byte sequence has a single encoding.
    if (digits == 2) {
        if ((group & 0x0FU) != 0) {
            throw_invalid_base64("nonzero bits after the last byte");
        }
        data.push_back(static_cast<std::byte>(group >> 4U));
    } else if (digits == 3) {
        if ((group & 0x03U) != 0) {
            throw_invalid_base64("nonzero bits after the last byte");
        }
        data.push_back(static_cast<std::byte>(group >> 10U));
        data.push_back(static_cast<std::byte>(group >> 2U));
    }
    return data;
}

std::string encode_hex(std::span<const std::byte> data)
{
    std::string text;
    text.reserve(data.size() * 2);
    for (const std::byte byte : data) {
        text += hex_digits[std::to_integer<unsigned>(byte) >> 4U];
        text += hex_digits[std::to_integer<unsigned>(byte) & 0x0FU];
    }
    return text;
}

std::vector<std::byte> decode_hex(std::string_view text)
{
    std::vector<std::byte> data;
    data.reserve(text.size() / 2);

    unsigned high = invalid_digit;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (is_white_space(text[i])) {
            continue;
        }
        const unsigned value = hex_value(text[i]);
        if (value == invalid_digit) {
            throw_invalid_character(errc::invalid_hex, "hexadecimal", text[i], i);
        }
        if (high == invalid_digit) {
            high = value;
        } else {
            data.push_back(static_cast<std::byte>((high << 4U) | value));
            high = invalid_digit;
        }
    }
    if (high != invalid_digit) {
        throw invalid_data_error(errc::invalid_hex, "invalid hexadecimal data: an odd number of digits");
    }
    return data;
}

} // namespace openxisf::detail
