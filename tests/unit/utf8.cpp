// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "core/utf8.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace {

using openxisf::detail::is_valid_utf8;

// The UTF-8 form of a code point, written from the bit layout of Unicode §3.9, table 3-6. Surrogates and code points
// above U+10FFFF get the same layout, which is how a careless encoder would write them.
std::string encode(char32_t code_point)
{
    const auto byte = [](std::uint32_t value) { return static_cast<char>(value); };
    const auto c = static_cast<std::uint32_t>(code_point);
    if (c < 0x80) {
        return {byte(c)};
    }
    if (c < 0x800) {
        return {byte(0xC0 | (c >> 6)), byte(0x80 | (c & 0x3F))};
    }
    if (c < 0x10000) {
        return {byte(0xE0 | (c >> 12)), byte(0x80 | ((c >> 6) & 0x3F)), byte(0x80 | (c & 0x3F))};
    }
    return {byte(0xF0 | (c >> 18)), byte(0x80 | ((c >> 12) & 0x3F)), byte(0x80 | ((c >> 6) & 0x3F)),
            byte(0x80 | (c & 0x3F))};
}

TEST(utf8, accepts_every_code_point_except_surrogates_and_u0000)
{
    for (char32_t c = 0; c <= 0x10FFFF; ++c) {
        const bool surrogate = c >= 0xD800 && c <= 0xDFFF;
        ASSERT_EQ(is_valid_utf8(encode(c)), c != 0 && !surrogate) << "U+" << std::hex << static_cast<std::uint32_t>(c);
    }
}

TEST(utf8, accepts_text_and_the_empty_string)
{
    EXPECT_TRUE(is_valid_utf8(""));
    EXPECT_TRUE(is_valid_utf8("  Ñandú — 星雲 ✓  "));
}

TEST(utf8, rejects_overlong_forms)
{
    for (const std::string_view overlong :
         {"\xC0\x80", "\xC1\xBF", "\xE0\x80\x80", "\xE0\x9F\xBF", "\xF0\x80\x80\x80", "\xF0\x8F\xBF\xBF"}) {
        EXPECT_FALSE(is_valid_utf8(overlong)) << testing::PrintToString(overlong);
    }
}

TEST(utf8, rejects_code_points_above_u10ffff)
{
    EXPECT_FALSE(is_valid_utf8(encode(0x110000)));
    EXPECT_FALSE(is_valid_utf8(encode(0x1FFFFF)));
    for (int lead = 0xF5; lead <= 0xFF; ++lead) {
        EXPECT_FALSE(is_valid_utf8(std::string{static_cast<char>(lead), '\x80', '\x80', '\x80'})) << lead;
    }
}

TEST(utf8, rejects_u0000)
{
    EXPECT_FALSE(is_valid_utf8(std::string_view("a\0b", 3)));
}

TEST(utf8, rejects_truncated_and_broken_sequences)
{
    for (const std::string_view broken :
         {"\x80", "\xBF", "\xC3", "\xE2\x82", "\xF0\x9F\x98", "\xE2\x28\xA1", "\xF0\x9F\x98\x28", "a\xC3", "\xC3!"}) {
        EXPECT_FALSE(is_valid_utf8(broken)) << testing::PrintToString(broken);
    }
}

TEST(utf8, judges_every_two_byte_string)
{
    for (int first = 0; first < 256; ++first) {
        for (int second = 0; second < 256; ++second) {
            const bool two_ascii = first > 0 && first < 0x80 && second > 0 && second < 0x80;
            const bool one_sequence = first >= 0xC2 && first <= 0xDF && second >= 0x80 && second <= 0xBF;
            const std::string text{static_cast<char>(first), static_cast<char>(second)};
            ASSERT_EQ(is_valid_utf8(text), two_ascii || one_sequence) << first << ' ' << second;
        }
    }
}

} // namespace
