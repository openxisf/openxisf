// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "core/uuid.h"

#include <openxisf/error.h>

#include "core/endian.h"
#include "core/quote.h"

#include <cstdint>
#include <span>

namespace openxisf::detail {

namespace {

// The canonical form is 8-4-4-4-12 hexadecimal digits, so hyphens follow bytes 4, 6, 8 and 10.
constexpr std::size_t canonical_length = 36;

bool hyphen_after(std::size_t byte_index) noexcept
{
    return byte_index == 4 || byte_index == 6 || byte_index == 8 || byte_index == 10;
}

// The value of a hexadecimal digit in either case, or 16.
unsigned digit_value(char c) noexcept
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

} // namespace

uuid make_uuid_v4(xoshiro256starstar& generator) noexcept
{
    uuid id{};
    store_big_endian(std::span(id).first<8>(), generator());
    store_big_endian(std::span(id).last<8>(), generator());
    // RFC 9562 §5.4: version 4 in the high nibble of byte 6, variant 10 in the high bits of byte 8.
    id[6] = (id[6] & std::byte{0x0F}) | std::byte{0x40};
    id[8] = (id[8] & std::byte{0x3F}) | std::byte{0x80};
    return id;
}

std::string format_uuid(const uuid& id)
{
    constexpr std::string_view hex_digits = "0123456789abcdef";
    std::string text;
    text.reserve(canonical_length);
    for (std::size_t i = 0; i < id.size(); ++i) {
        if (hyphen_after(i)) {
            text += '-';
        }
        text += hex_digits[std::to_integer<unsigned>(id[i]) >> 4U];
        text += hex_digits[std::to_integer<unsigned>(id[i]) & 0x0FU];
    }
    return text;
}

uuid parse_uuid(std::string_view text)
{
    const auto invalid = [text] {
        return invalid_data_error(errc::invalid_uuid, quote(text) + " is not a UUID in canonical form");
    };
    if (text.size() != canonical_length) {
        throw invalid();
    }

    uuid id{};
    std::size_t position = 0;
    for (std::size_t i = 0; i < id.size(); ++i) {
        if (hyphen_after(i) && text[position++] != '-') {
            throw invalid();
        }
        const unsigned high = digit_value(text[position++]);
        const unsigned low = digit_value(text[position++]);
        if (high > 15 || low > 15) {
            throw invalid();
        }
        id[i] = static_cast<std::byte>((high << 4U) | low);
    }
    return id;
}

} // namespace openxisf::detail
