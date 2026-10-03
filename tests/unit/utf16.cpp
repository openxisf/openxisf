// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "core/utf16.h"

#include "support/throws.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace {

using openxisf::errc;
using openxisf::invalid_data_error;
using openxisf::detail::utf16_to_utf8;
using openxisf::detail::utf8_to_utf16;
using openxisf::test::throws;

// The UTF-8 and UTF-16 forms of a code point, written from the bit layouts of Unicode §3.9, tables 3-5 and 3-6.
std::string encode_utf8(std::uint32_t c)
{
    const auto byte = [](std::uint32_t value) { return static_cast<char>(value); };
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

std::u16string encode_utf16(std::uint32_t c)
{
    if (c < 0x10000) {
        return {static_cast<char16_t>(c)};
    }
    // uuuuu xxxxxx yyyyyyyyyy becomes 110110wwwwxxxxxx 110111yyyyyyyyyy, with wwww = uuuuu - 1.
    return {static_cast<char16_t>(0xD800 | (((c >> 16) - 1) << 6) | ((c >> 10) & 0x3F)),
            static_cast<char16_t>(0xDC00 | (c & 0x3FF))};
}

TEST(utf16, converts_text_in_both_directions)
{
    // Latin and CJK characters, and one outside the BMP, which takes a surrogate pair. The compiler encodes the
    // literals.
    const std::string_view utf8 = "Ñandú 星雲 𝔛";
    const std::u16string_view utf16 = u"Ñandú 星雲 𝔛";

    EXPECT_EQ(utf8_to_utf16(utf8), utf16);
    EXPECT_EQ(utf16_to_utf8(utf16), utf8);
    EXPECT_EQ(utf8_to_utf16(""), u"");
    EXPECT_EQ(utf16_to_utf8(u""), "");
}

TEST(utf16, converts_every_code_point)
{
    for (std::uint32_t c = 1; c <= 0x10FFFF; ++c) {
        if (c >= 0xD800 && c <= 0xDFFF) {
            continue;
        }
        const std::string utf8 = encode_utf8(c);
        const std::u16string utf16 = encode_utf16(c);
        ASSERT_EQ(utf8_to_utf16(utf8), utf16) << "U+" << std::hex << c;
        ASSERT_EQ(utf16_to_utf8(utf16), utf8) << "U+" << std::hex << c;
    }
}

TEST(utf16, rejects_text_that_is_not_utf8)
{
    for (const std::string_view invalid :
         {std::string_view("\xC3"), std::string_view("\xC0\x80"), std::string_view("\xED\xA0\x80"),
          std::string_view("\xF4\x90\x80\x80"), std::string_view("a\0b", 3)}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_utf8, [&] { (void)utf8_to_utf16(invalid); }))
            << testing::PrintToString(invalid);
    }
}

TEST(utf16, rejects_unpaired_surrogates_and_u0000)
{
    const char16_t high = 0xD800;
    const char16_t low = 0xDC00;
    for (const std::u16string& invalid :
         {std::u16string{high}, std::u16string{low}, std::u16string{high, u'a'}, std::u16string{low, high},
          std::u16string{u'a', 0xDBFF}, std::u16string{u'a', 0, u'b'}}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_utf16, [&] { (void)utf16_to_utf8(invalid); }));
    }
}

} // namespace
