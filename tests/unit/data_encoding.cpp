// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "core/data_encoding.h"

#include "support/throws.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

using openxisf::errc;
using openxisf::invalid_data_error;
using openxisf::detail::decode_base64;
using openxisf::detail::decode_hex;
using openxisf::detail::encode_base64;
using openxisf::detail::encode_hex;
using openxisf::test::throws;

std::vector<std::byte> bytes(std::string_view text)
{
    std::vector<std::byte> data;
    for (const char c : text) {
        data.push_back(static_cast<std::byte>(c));
    }
    return data;
}

testing::AssertionResult invalid_base64(std::string_view text)
{
    return throws<invalid_data_error>(errc::invalid_base64, [text] { (void)decode_base64(text); });
}

testing::AssertionResult invalid_hex(std::string_view text)
{
    return throws<invalid_data_error>(errc::invalid_hex, [text] { (void)decode_hex(text); });
}

// The test vectors of RFC 4648, §10.
struct test_vector
{
    std::string_view data;
    std::string_view base64;
    std::string_view hex;
};

constexpr std::array rfc4648_vectors{
    test_vector{.data = "", .base64 = "", .hex = ""},
    test_vector{.data = "f", .base64 = "Zg==", .hex = "66"},
    test_vector{.data = "fo", .base64 = "Zm8=", .hex = "666f"},
    test_vector{.data = "foo", .base64 = "Zm9v", .hex = "666f6f"},
    test_vector{.data = "foob", .base64 = "Zm9vYg==", .hex = "666f6f62"},
    test_vector{.data = "fooba", .base64 = "Zm9vYmE=", .hex = "666f6f6261"},
    test_vector{.data = "foobar", .base64 = "Zm9vYmFy", .hex = "666f6f626172"},
};

TEST(data_encoding, base64_test_vectors_of_rfc_4648)
{
    for (const test_vector& v : rfc4648_vectors) {
        EXPECT_EQ(encode_base64(bytes(v.data)), v.base64) << v.data;
        EXPECT_EQ(decode_base64(v.base64), bytes(v.data)) << v.data;
    }
}

TEST(data_encoding, hex_test_vectors_of_rfc_4648_in_lowercase)
{
    for (const test_vector& v : rfc4648_vectors) {
        EXPECT_EQ(encode_hex(bytes(v.data)), v.hex) << v.data;
        EXPECT_EQ(decode_hex(v.hex), bytes(v.data)) << v.data;
    }
}

TEST(data_encoding, inline_and_embedded_blocks_of_the_specification)
{
    EXPECT_EQ(decode_base64("\n   VGhpcyBpcyBhIHRlc3QgLSBURVNUIC0gMTIzNDU2Nzg5MA==\n"),
              bytes("This is a test - TEST - 1234567890"));

    // A 6 × 6 RGB image, split across lines.
    const std::vector<std::byte> image =
        decode_base64("\n      AAAAAP8A/wD/AAAAAAAAAP8AAP8AAAAAAAAA/wD/AP8AAAAA/wD//wD/AP8AAP8A/wD//wD//"
                      "\n      wD//wD/AP8AAP8A/wD//wD/AP8AAAAAAAAA/wD/AP8AAAAAAAAAAP8A/wD/AAAAAAAAAP8A\n   ");
    ASSERT_EQ(image.size(), 6U * 6U * 3U);
    EXPECT_EQ(std::vector<std::byte>(image.begin(), image.begin() + 6), bytes(std::string_view("\0\0\0\0\xFF\0", 6)));
}

TEST(data_encoding, every_byte_value_and_length_round_trips)
{
    std::vector<std::byte> data(256);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::byte>(i);
    }
    for (std::size_t length = 0; length <= data.size(); ++length) {
        const std::vector<std::byte> part(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(length));
        ASSERT_EQ(decode_base64(encode_base64(part)), part) << length;
        ASSERT_EQ(decode_hex(encode_hex(part)), part) << length;
    }
}

TEST(data_encoding, white_space_is_ignored_anywhere)
{
    EXPECT_EQ(decode_base64(" Zm9v\nYmFy\t"), bytes("foobar"));
    EXPECT_EQ(decode_base64("Z m 9 v"), bytes("foo"));
    EXPECT_EQ(decode_base64("Zg= =\r\n"), bytes("f"));
    EXPECT_EQ(decode_base64(" \t\r\n"), bytes(""));
    EXPECT_EQ(decode_hex("66 6f\n6f"), bytes("foo"));
    EXPECT_EQ(decode_hex("6 6"), bytes("f"));
    EXPECT_EQ(decode_hex(" \n "), bytes(""));
}

TEST(data_encoding, base64_rejects_characters_outside_the_alphabet)
{
    using namespace std::string_view_literals;
    for (const std::string_view text :
         {"Zm9v!"sv, "Zm9v-_8="sv, "Zm9v\xC3\xA9"sv, "Zm\0v"sv, "Zm9v."sv, "Zm9v\xC2\xA0"sv}) {
        EXPECT_TRUE(invalid_base64(text)) << testing::PrintToString(text);
    }
}

TEST(data_encoding, base64_requires_exact_padding)
{
    for (const std::string_view text : {"Zg", "Zg=", "Zg===", "Zm8", "Zm8==", "Zm9v=", "=Zm9", "Zg==Zg==", "Zg==x", "Z",
                                        "Z===", "Zm9vY", "Zm9vY===", "===="}) {
        EXPECT_TRUE(invalid_base64(text)) << text;
    }
}

TEST(data_encoding, base64_requires_zero_bits_after_the_last_byte)
{
    EXPECT_TRUE(invalid_base64("Zh=="));
    EXPECT_TRUE(invalid_base64("Zm9="));
    EXPECT_EQ(decode_base64("Zg=="), bytes("f"));
    EXPECT_EQ(decode_base64("Zm8="), bytes("fo"));
}

TEST(data_encoding, hex_requires_lowercase_digits_in_pairs)
{
    for (const std::string_view text : {"666F", "6A", "6", "666", "6g", "0x66", "66-6f", "\xC3\xA9"}) {
        EXPECT_TRUE(invalid_hex(text)) << testing::PrintToString(text);
    }
}

TEST(data_encoding, errors_tell_the_position_of_an_invalid_character)
{
    try {
        (void)decode_base64("Zm9v Y!");
        FAIL() << "no exception";
    } catch (const invalid_data_error& failure) {
        EXPECT_STREQ(failure.what(), "invalid Base64 data: character '!' at position 6");
    }
}

} // namespace
