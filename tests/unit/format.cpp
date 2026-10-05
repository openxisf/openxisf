// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Spec §8.4.3 and §11.1.2: property values as text, with property format specifiers.

#include <openxisf/error.h>
#include <openxisf/format.h>
#include <openxisf/property.h>

#include "support/throws.h"

#include <gtest/gtest.h>

#include <complex>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

using openxisf::errc;
using openxisf::format_align;
using openxisf::format_base;
using openxisf::format_bool;
using openxisf::format_element;
using openxisf::format_notation;
using openxisf::format_sign;
using openxisf::format_value;
using openxisf::property_format;
using openxisf::property_value;
using openxisf::usage_error;
using openxisf::test::throws;

constexpr double infinity = std::numeric_limits<double>::infinity();

property_format fixed(std::uint32_t precision)
{
    return {.precision = precision, .notation = format_notation::fixed};
}

property_format scientific(std::uint32_t precision)
{
    return {.precision = precision, .notation = format_notation::scientific};
}

property_format automatic(std::uint32_t precision)
{
    return {.precision = precision};
}

property_format in_base(format_base base)
{
    return {.base = base};
}

TEST(format, the_examples_of_the_specification)
{
    // Spec §11.1.2.
    EXPECT_EQ(format_value(std::uint32_t{2540}, {.width = 8, .align = format_align::center}), "  2540  ");
    EXPECT_EQ(format_value(
                  1.25F, {.width = 6, .sign = format_sign::force, .precision = 2, .notation = format_notation::fixed}),
              " +1.25");
    EXPECT_EQ(format_value(std::uint16_t{0xff39}, {.width = 8, .fill = '.', .base = format_base::hexadecimal}),
              "....ff39");
    // A vector as comma-separated components, as the specification assumes.
    EXPECT_EQ(format_value(std::vector<double>{32.1, -1.24e-08}, {.width = 10, .precision = 3}),
              "      32.1, -1.24e-08");
}

TEST(format, padding_follows_the_width_the_fill_and_the_alignment)
{
    EXPECT_EQ(format_value("ab", {.width = 5}), "   ab");
    EXPECT_EQ(format_value("ab", {.width = 5, .align = format_align::left}), "ab   ");
    // Odd padding: one more character before than after.
    EXPECT_EQ(format_value("ab", {.width = 5, .align = format_align::center}), "  ab ");
    EXPECT_EQ(format_value("ab", {.width = 4, .align = format_align::center}), " ab ");
    EXPECT_EQ(format_value("ab", {.width = 4, .fill = '*', .align = format_align::left}), "ab**");
    // A width below the length pads nothing and cuts nothing.
    EXPECT_EQ(format_value("abcdef", {.width = 3}), "abcdef");
    EXPECT_EQ(format_value(std::int32_t{-7}, {.width = 4, .fill = '0'}), "00-7");
    // The width counts characters, not bytes: Ñandú has five, of one and two bytes, and 星雲 two, of three bytes.
    EXPECT_EQ(format_value("\xE6\x98\x9F\xE9\x9B\xB2", {.width = 4}), "  \xE6\x98\x9F\xE9\x9B\xB2");
    EXPECT_EQ(format_value("\xC3\x91"
                           "and\xC3\xBA",
                           {.width = 7}),
              "  \xC3\x91"
              "and\xC3\xBA");
}

TEST(format, a_sign_marks_numbers_as_the_mode_says)
{
    const property_format force{.sign = format_sign::force};
    EXPECT_EQ(format_value(std::int16_t{-5}), "-5");
    EXPECT_EQ(format_value(std::int16_t{5}), "5");
    EXPECT_EQ(format_value(std::int16_t{5}, force), "+5");
    EXPECT_EQ(format_value(std::uint8_t{5}, force), "+5");
    EXPECT_EQ(format_value(-2.5, force), "-2.5");
    EXPECT_EQ(format_value(2.5, force), "+2.5");
    EXPECT_EQ(
        format_value(1e-300, {.sign = format_sign::force, .precision = 2, .notation = format_notation::scientific}),
        "+1.00e-300");
}

TEST(format, a_value_represented_as_zero_has_no_sign)
{
    // Spec §8.4.3: after rounding, with either mode.
    const property_format force{.sign = format_sign::force};
    EXPECT_EQ(format_value(std::int64_t{0}, force), "0");
    EXPECT_EQ(format_value(0.0, force), "0");
    EXPECT_EQ(format_value(-0.0), "0");
    EXPECT_EQ(format_value(-0.001, fixed(2)), "0.00");
    EXPECT_EQ(format_value(0.004, {.sign = format_sign::force, .precision = 2, .notation = format_notation::fixed}),
              "0.00");
    EXPECT_EQ(format_value(-0.0, scientific(1)), "0.0e+00");
}

TEST(format, fixed_and_scientific_notations_have_precision_digits_after_the_point)
{
    EXPECT_EQ(format_value(1.25, fixed(6)), "1.250000");
    EXPECT_EQ(format_value(2.7, fixed(0)), "3");
    EXPECT_EQ(format_value(1234.5678, fixed(2)), "1234.57");
    EXPECT_EQ(format_value(1.25, scientific(6)), "1.250000e+00");
    EXPECT_EQ(format_value(2700.0, scientific(0)), "3e+03");
    EXPECT_EQ(format_value(-1.24e-8, scientific(2)), "-1.24e-08");
    EXPECT_EQ(format_value(6.02e23, scientific(3)), "6.020e+23");
    // Digits beyond those of a double are those of its exact value.
    EXPECT_EQ(format_value(0.1, fixed(20)), "0.10000000000000000555");
}

TEST(format, automatic_notation_is_the_g_conversion_of_printf)
{
    // Fixed when the exponent X satisfies -4 <= X < P, scientific otherwise, without trailing zeros.
    EXPECT_EQ(format_value(123456.0), "123456");
    EXPECT_EQ(format_value(1234567.0), "1.23457e+06");
    EXPECT_EQ(format_value(0.0001), "0.0001");
    EXPECT_EQ(format_value(0.00001), "1e-05");
    EXPECT_EQ(format_value(1.5), "1.5");
    EXPECT_EQ(format_value(2.0), "2");
    EXPECT_EQ(format_value(100.0, automatic(1)), "1e+02");
    EXPECT_EQ(format_value(100.0, automatic(3)), "100");
    // A precision of zero is one.
    EXPECT_EQ(format_value(2.7, automatic(0)), "3");
    EXPECT_EQ(format_value(0.000123456789, automatic(3)), "0.000123");
    EXPECT_EQ(format_value(1.0 / 3.0, automatic(17)), "0.33333333333333331");
}

TEST(format, float32_values_are_formatted_as_they_are)
{
    EXPECT_EQ(format_value(0.1F), "0.1");
    EXPECT_EQ(format_value(0.1F, automatic(10)), "0.1000000015");
    EXPECT_EQ(format_value(std::numeric_limits<float>::max(), automatic(3)), "3.4e+38");
}

TEST(format, non_finite_values_are_inf_and_nan)
{
    EXPECT_EQ(format_value(infinity), "inf");
    EXPECT_EQ(format_value(-infinity), "-inf");
    EXPECT_EQ(format_value(infinity, {.sign = format_sign::force}), "+inf");
    EXPECT_EQ(format_value(std::numeric_limits<double>::quiet_NaN(), {.sign = format_sign::force}), "nan");
    EXPECT_EQ(format_value(-std::numeric_limits<double>::quiet_NaN()), "nan");
    EXPECT_EQ(format_value(-std::numeric_limits<float>::infinity(), {.width = 6, .notation = format_notation::fixed}),
              "  -inf");
}

TEST(format, booleans_are_words_or_digits)
{
    EXPECT_EQ(format_value(true), "true");
    EXPECT_EQ(format_value(false), "false");
    EXPECT_EQ(format_value(true, {.boolean = format_bool::numeric}), "1");
    EXPECT_EQ(format_value(false, {.boolean = format_bool::numeric}), "0");
    // Not numbers: no sign, no base.
    EXPECT_EQ(format_value(true, {.sign = format_sign::force, .boolean = format_bool::numeric}), "1");
    EXPECT_EQ(format_value(true, {.width = 6, .base = format_base::binary}), "  true");
}

TEST(format, integers_are_written_in_the_base_of_the_format)
{
    EXPECT_EQ(format_value(std::uint8_t{255}, in_base(format_base::binary)), "11111111");
    EXPECT_EQ(format_value(std::uint8_t{255}, in_base(format_base::octal)), "377");
    EXPECT_EQ(format_value(std::uint8_t{255}, in_base(format_base::decimal)), "255");
    EXPECT_EQ(format_value(std::uint8_t{255}, in_base(format_base::hexadecimal)), "ff");
    EXPECT_EQ(format_value(std::uint8_t{0}, in_base(format_base::binary)), "0");
    // A negative integer is a sign and its magnitude.
    EXPECT_EQ(format_value(std::int32_t{-255}, in_base(format_base::hexadecimal)), "-ff");
    EXPECT_EQ(format_value(std::int8_t{-128}, in_base(format_base::binary)), "-10000000");
    EXPECT_EQ(format_value(std::numeric_limits<std::int64_t>::min()), "-9223372036854775808");
    EXPECT_EQ(format_value(std::numeric_limits<std::uint64_t>::max(), in_base(format_base::hexadecimal)),
              "ffffffffffffffff");
    // Precision and notation are for floating point values.
    EXPECT_EQ(format_value(std::int32_t{42}, fixed(2)), "42");
}

TEST(format, integers_of_128_bits)
{
    using openxisf::int128;
    using openxisf::uint128;
    constexpr std::uint64_t ones = std::numeric_limits<std::uint64_t>::max();
    EXPECT_EQ(format_value(uint128{.high = ones, .low = ones}), "340282366920938463463374607431768211455");
    EXPECT_EQ(format_value(uint128{.high = ones, .low = ones}, in_base(format_base::hexadecimal)),
              std::string(32, 'f'));
    EXPECT_EQ(format_value(uint128{.high = 1, .low = 0}), "18446744073709551616");
    EXPECT_EQ(format_value(uint128{.high = 1, .low = 0}, in_base(format_base::octal)), "2000000000000000000000");
    EXPECT_EQ(format_value(int128{.high = std::numeric_limits<std::int64_t>::min(), .low = 0}),
              "-170141183460469231731687303715884105728");
    EXPECT_EQ(format_value(int128{.high = -1, .low = ones}), "-1");
    EXPECT_EQ(format_value(int128{.high = -1, .low = 0}, in_base(format_base::hexadecimal)), "-10000000000000000");
    EXPECT_EQ(format_value(int128{.high = 0, .low = 5}, {.sign = format_sign::force}), "+5");
    EXPECT_EQ(format_value(int128{}, {.sign = format_sign::force}), "0");
}

TEST(format, strings_take_only_the_width_the_fill_and_the_alignment)
{
    EXPECT_EQ(format_value("abc", {.sign = format_sign::force, .precision = 1, .base = format_base::binary}), "abc");
    EXPECT_EQ(format_value(std::string()), "");
    EXPECT_EQ(format_value("  spaced  "), "  spaced  ");
}

TEST(format, the_unit_is_not_appended)
{
    EXPECT_EQ(format_value(3.0, {.unit = "m/s"}), "3");
}

TEST(format, complex_numbers_format_each_part)
{
    EXPECT_EQ(format_value(std::complex<double>(1.5, -2.25)), "(1.5,-2.25)");
    EXPECT_EQ(format_value(std::complex<float>(1.5F, -2.25F), {.width = 6}), "(   1.5, -2.25)");
    EXPECT_EQ(format_value(std::complex<double>(1.5, 0.0), {.sign = format_sign::force}), "(+1.5,0)");
}

TEST(format, vectors_and_matrices_format_each_element)
{
    EXPECT_EQ(format_value(std::vector<std::int32_t>{1, -2, 3}), "1,-2,3");
    EXPECT_EQ(format_value(std::vector<double>{}), "");
    EXPECT_EQ(format_value(std::vector<std::complex<float>>{{1.0F, 2.0F}, {-3.0F, 4.0F}}), "(1,2),(-3,4)");
    const property_value matrix = property_value::matrix<std::uint16_t>(2, 2, {1, 2, 3, 4});
    EXPECT_EQ(format_value(matrix), "1,2;3,4");
    EXPECT_EQ(format_value(matrix, {.width = 2}), " 1, 2; 3, 4");
    EXPECT_EQ(format_value(property_value::matrix<double>(0, 0, {})), "");
    EXPECT_EQ(format_value(property_value::matrix<std::int8_t>(1, 3, {1, 2, 3})), "1,2,3");
}

TEST(format, one_element_of_a_vector_or_a_matrix)
{
    const property_value vector = std::vector<double>{32.1, -1.24e-08};
    EXPECT_EQ(format_element(vector, 1, {.width = 10, .precision = 3}), " -1.24e-08");
    const property_value matrix = property_value::matrix<std::uint16_t>(2, 2, {1, 2, 3, 4});
    EXPECT_EQ(format_element(matrix, 3, in_base(format_base::binary)), "100");
    EXPECT_EQ(format_element(std::vector<std::complex<double>>{{1.0, 2.0}}, 0), "(1,2)");
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [&] { (void)format_element(vector, 2); }));
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [&] { (void)format_element(matrix, 4); }));
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [] { (void)format_element(property_value(1.0), 0); }));
}

TEST(format, time_points_take_no_format)
{
    // Spec §8.4.3.1.
    const openxisf::date_time time{
        .year = 2026, .month = 3, .day = 14, .hour = 1, .minute = 59, .second = 26, .nanosecond = 535000000};
    EXPECT_EQ(format_value(time, {.width = 40, .fill = '*'}), "2026-03-14T01:59:26.535Z");
    // The years that a reader returns for 0000-01-01T00:30:00+01:00 and 9999-12-31T23:30:00-01:00.
    const openxisf::date_time before{.year = -1, .month = 12, .day = 31, .hour = 23, .minute = 30};
    const openxisf::date_time after{.year = 10000, .month = 1, .day = 1, .hour = 0, .minute = 30};
    EXPECT_EQ(format_value(before), "-0001-12-31T23:30:00Z");
    EXPECT_EQ(format_value(after), "10000-01-01T00:30:00Z");
    for (const openxisf::date_time& invalid :
         {openxisf::date_time{.year = 2026, .month = 13}, openxisf::date_time{.year = -2, .month = 12, .day = 31},
          openxisf::date_time{.year = -1, .month = 2, .day = 29}, openxisf::date_time{.year = 10001}}) {
        EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [&] { (void)format_value(invalid); }))
            << invalid.year << '-' << invalid.month << '-' << invalid.day;
    }
}

TEST(format, float128_values_are_formatted_from_their_nearest_double)
{
    EXPECT_EQ(format_value(property_value::from_float128_text("1.5"), fixed(2)), "1.50");
    EXPECT_EQ(format_value(property_value::from_float128_text("-1e-400")), "0");
    EXPECT_EQ(format_value(property_value::from_float128_text("1e400")), "inf");
    EXPECT_EQ(format_value(property_value::from_float128_text("-1e400")), "-inf");
    EXPECT_EQ(format_value(property_value::from_complex128_text("( 1.5 , -2 )")), "(1.5,-2)");
    // 1 and -2 as binary128 values: the exponent bias is 16383.
    const openxisf::float128 one{.high = 0x3FFF000000000000, .low = 0};
    const openxisf::float128 minus_two{.high = 0xC000000000000000, .low = 0};
    EXPECT_EQ(format_value(std::vector<openxisf::float128>{one, minus_two}), "1,-2");
    EXPECT_EQ(format_value(std::vector<openxisf::complex128>{{.real = one, .imag = minus_two}}), "(1,-2)");
}

TEST(format, width_and_precision_are_bounded)
{
    // A format read from a file may ask for 2^32 - 1 characters.
    EXPECT_EQ(format_value("x", {.width = 4294967295U}).size(), 1024U);
    EXPECT_EQ(format_value(1.0, fixed(4294967295U)), "1." + std::string(1024, '0'));
    EXPECT_EQ(format_value(-1.0, scientific(100000)).size(), 1U + 2U + 1024U + 4U);
    const std::string widest = format_value(-std::numeric_limits<double>::max(), fixed(4294967295U));
    EXPECT_EQ(widest.size(), 1U + 309U + 1U + 1024U);
}

} // namespace
