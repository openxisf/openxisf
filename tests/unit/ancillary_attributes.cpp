// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Spec §8.5.4.1, §8.5.6 and §11.6 to §11.11: the attributes of the elements that describe images.

#include "model/ancillary_attributes.h"

#include <openxisf/error.h>
#include <openxisf/image.h>

#include "support/opened_unit.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace {

using openxisf::errc;
using openxisf::invalid_data_error;
using openxisf::test::throws;
namespace detail = openxisf::detail;

// -----------------------------------------------------------------------------------------------------------------
// FITS keywords (spec §11.6, FITS 4.0 §4.1.2.1)

TEST(ancillary_attributes, fits_keyword_names_follow_the_fits_standard_without_padding)
{
    for (const std::string_view name :
         {"OBJECT", "DATE-OBS", "NAXIS1", "CCD-TEMP", "A_B", "12345678", "-_-", "X", ""}) {
        EXPECT_TRUE(detail::is_fits_keyword_name(name)) << name;
    }
    // Lower case, padding, more than eight characters, and characters outside the set.
    for (const std::string_view name :
         {"object", "OBJECT ", " OBJECT", "OBJ ECT", "TOOLONGNM", "DATE.OBS", "N\xC3\x91", "HIERARCH X", "A\tB"}) {
        EXPECT_FALSE(detail::is_fits_keyword_name(name)) << name;
    }
}

TEST(ancillary_attributes, comment_history_and_blank_keywords_have_no_value)
{
    EXPECT_TRUE(detail::is_commentary_keyword("COMMENT"));
    EXPECT_TRUE(detail::is_commentary_keyword("HISTORY"));
    EXPECT_TRUE(detail::is_commentary_keyword(""));
    EXPECT_FALSE(detail::is_commentary_keyword("comment"));
    EXPECT_FALSE(detail::is_commentary_keyword("OBJECT"));
}

// -----------------------------------------------------------------------------------------------------------------
// RGB working spaces (spec §8.5.4.1, §11.8)

TEST(ancillary_attributes, a_gamma_is_a_positive_value_or_srgb_in_any_case)
{
    EXPECT_EQ(detail::parse_gamma("2.2"), std::optional(2.2));
    EXPECT_EQ(detail::parse_gamma(" 1 "), std::optional(1.0));
    for (const std::string_view text : {"sRGB", "srgb", "SRGB", "SrGb", " sRGB "}) {
        EXPECT_EQ(detail::parse_gamma(text), std::nullopt) << text;
    }
    for (const std::string_view text : {"0", "-1", "-0", "inf", "NaN", "", "2.2x", "sRGB2", "s RGB"}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_rgb_working_space, [text] {
            (void)detail::parse_gamma(text);
        })) << text;
    }
}

TEST(ancillary_attributes, a_list_of_values_has_exactly_its_items_each_finite)
{
    EXPECT_EQ(detail::parse_triplet("0.648431:0.321152:0.155886", errc::invalid_rgb_working_space),
              (std::array<double, 3>{0.648431, 0.321152, 0.155886}));
    EXPECT_EQ(detail::parse_triplet(" 1 :2: 3", errc::invalid_rgb_working_space),
              (std::array<double, 3>{1.0, 2.0, 3.0}));
    EXPECT_EQ(detail::parse_quadruplet("0.5:0:1:-0.25", errc::invalid_display_function),
              (std::array<double, 4>{0.5, 0.0, 1.0, -0.25}));
    for (const std::string_view text : {"1:2", "1:2:3:4", "1::3", ":2:3", "1:2:", "a:b:c", "1:2:inf", "1:NaN:3", ""}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_rgb_working_space, [text] {
            (void)detail::parse_triplet(text, errc::invalid_rgb_working_space);
        })) << text;
    }
    EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_display_function, [] {
        (void)detail::parse_quadruplet("1:2:3", errc::invalid_display_function);
    }));
}

TEST(ancillary_attributes, the_luminance_coefficients_follow_from_the_chromaticities_and_d50)
{
    // The examples of spec §11.8.2, written with six decimals: sRGB and Adobe RGB (1998) relative to D50.
    const std::optional<std::array<double, 3>> srgb_derived =
        detail::derive_luminance({0.648431, 0.321152, 0.155886}, {0.330856, 0.597871, 0.066044});
    const std::optional<std::array<double, 3>> adobe_derived =
        detail::derive_luminance({0.648431, 0.230154, 0.155886}, {0.330856, 0.701572, 0.066044});
    const std::array<double, 3>& srgb = openxisf::test::value_of(srgb_derived);
    const std::array<double, 3>& adobe = openxisf::test::value_of(adobe_derived);
    const std::array<double, 3> srgb_y{0.222491, 0.716888, 0.060621};
    const std::array<double, 3> adobe_y{0.311114, 0.625662, 0.063224};
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(srgb[i], srgb_y[i], 1e-6) << i;
        EXPECT_NEAR(adobe[i], adobe_y[i], 1e-6) << i;
    }
    EXPECT_NEAR(srgb[0] + srgb[1] + srgb[2], 1.0, 1e-12);
}

TEST(ancillary_attributes, chromaticities_without_a_solution_define_no_working_space)
{
    // A y coordinate of zero, and three equal primaries, whose system is singular.
    EXPECT_EQ(detail::derive_luminance({0.6, 0.3, 0.15}, {0.3, 0.6, 0.0}), std::nullopt);
    EXPECT_EQ(detail::derive_luminance({0.3, 0.3, 0.3}, {0.3, 0.3, 0.3}), std::nullopt);
}

TEST(ancillary_attributes, a_working_space_has_values_in_the_unit_range_and_a_solution)
{
    const openxisf::rgb_working_space srgb;
    const std::array<double, 3> derived = detail::check_rgb_working_space(srgb);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(derived[i], srgb.luminance[i], detail::luminance_tolerance);
    }
    const auto refused = [](const openxisf::rgb_working_space& space) {
        return throws<invalid_data_error>(errc::invalid_rgb_working_space,
                                          [&space] { (void)detail::check_rgb_working_space(space); });
    };
    EXPECT_TRUE(refused({.x = {1.2, 0.3, 0.15}}));
    EXPECT_TRUE(refused({.y = {0.33, 0.6, -0.01}}));
    EXPECT_TRUE(refused({.luminance = {0.2, 0.8, 1.5}}));
    EXPECT_TRUE(refused({.x = {0.3, 0.3, 0.3}, .y = {0.3, 0.3, 0.3}}));
    EXPECT_TRUE(refused({.y = {0.33, 0.6, 0.0}}));
}

// -----------------------------------------------------------------------------------------------------------------
// Display functions (spec §8.5.6, §11.9)

TEST(ancillary_attributes, a_display_function_keeps_its_parameters_in_range)
{
    EXPECT_NO_THROW(detail::check_display_function({}));
    // The example of spec §11.9.2, and the extremes of each range.
    EXPECT_NO_THROW(detail::check_display_function(
        {.midtones = {0.000735, 0.000735, 0.000735, 0.5}, .shadows = {0.003758, 0.003758, 0.003758, 0.0}}));
    EXPECT_NO_THROW(detail::check_display_function({.midtones = {0.0, 1.0, 0.5, 0.5},
                                                    .shadows = {0.5, 0.0, 1.0, 0.0},
                                                    .highlights = {0.5, 0.0, 1.0, 1.0},
                                                    .shadows_expansion = {-5.0, 0.0, 0.0, 0.0},
                                                    .highlights_expansion = {5.0, 1.0, 1.0, 1.0}}));
    const auto refused = [](const openxisf::display_function& function) {
        return throws<invalid_data_error>(errc::invalid_display_function,
                                          [&function] { detail::check_display_function(function); });
    };
    EXPECT_TRUE(refused({.midtones = {0.5, 0.5, 0.5, 1.5}}));
    EXPECT_TRUE(refused({.midtones = {-0.1, 0.5, 0.5, 0.5}}));
    EXPECT_TRUE(refused({.shadows = {0.0, -0.1, 0.0, 0.0}}));
    EXPECT_TRUE(refused({.highlights = {1.0, 1.0, 1.1, 1.0}}));
    EXPECT_TRUE(refused({.shadows = {0.6, 0.0, 0.0, 0.0}, .highlights = {0.5, 1.0, 1.0, 1.0}}));
    EXPECT_TRUE(refused({.shadows_expansion = {0.0, 0.0, 0.0, 0.1}}));
    EXPECT_TRUE(refused({.highlights_expansion = {1.0, 0.9, 1.0, 1.0}}));
}

// -----------------------------------------------------------------------------------------------------------------
// Colour filter arrays (spec §11.10)

TEST(ancillary_attributes, a_cfa_size_is_an_unsigned_integer_above_zero)
{
    EXPECT_EQ(detail::parse_cfa_size("2"), 2U);
    EXPECT_EQ(detail::parse_cfa_size(" 0x6 "), 6U);
    for (const std::string_view text : {"0", "-2", "2.0", "two", ""}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_color_filter_array, [text] {
            (void)detail::parse_cfa_size(text);
        })) << text;
    }
}

TEST(ancillary_attributes, a_cfa_pattern_has_one_element_of_table_18_for_each_pixel)
{
    // The examples of spec §11.10.2.
    EXPECT_NO_THROW(detail::check_color_filter_array({.pattern = "GRBG", .width = 2, .height = 2}));
    EXPECT_NO_THROW(
        detail::check_color_filter_array({.pattern = "GBGGRGRGRBGBGBGGRGGRGGBGBGBRGRGRGGBG", .width = 6, .height = 6}));
    EXPECT_NO_THROW(detail::check_color_filter_array({.pattern = "GWRWWGWRBWGWWBWG", .width = 4, .height = 4}));
    EXPECT_NO_THROW(detail::check_color_filter_array({.pattern = "0RGBWCMY", .width = 8, .height = 1}));

    const auto refused = [](const openxisf::color_filter_array& filter) {
        return throws<invalid_data_error>(errc::invalid_color_filter_array,
                                          [&filter] { detail::check_color_filter_array(filter); });
    };
    EXPECT_TRUE(refused({.pattern = "RGGb", .width = 2, .height = 2}));
    EXPECT_TRUE(refused({.pattern = "RGG ", .width = 2, .height = 2}));
    EXPECT_TRUE(refused({.pattern = "RGGBR", .width = 2, .height = 2}));
    EXPECT_TRUE(refused({.pattern = "RGG", .width = 2, .height = 2}));
    EXPECT_TRUE(refused({.pattern = "", .width = 0, .height = 0}));
    EXPECT_TRUE(refused({.pattern = "R", .width = 1, .height = 0}));
    // 2^32 x 2^32 overflows 64 bits, and is not the length of the pattern.
    EXPECT_TRUE(refused({.pattern = "R", .width = std::uint64_t{1} << 32U, .height = std::uint64_t{1} << 32U}));
}

// -----------------------------------------------------------------------------------------------------------------
// Resolution (spec §11.11)

TEST(ancillary_attributes, a_resolution_is_positive_in_inches_or_centimetres)
{
    EXPECT_EQ(detail::parse_resolution_value("72"), 72.0);
    EXPECT_EQ(detail::parse_resolution_value("0.5"), 0.5);
    for (const std::string_view text : {"0", "-72", "inf", "NaN", "x", ""}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_resolution, [text] {
            (void)detail::parse_resolution_value(text);
        })) << text;
    }
    EXPECT_EQ(detail::parse_resolution_unit("inch"), openxisf::resolution_unit::inch);
    EXPECT_EQ(detail::parse_resolution_unit("cm"), openxisf::resolution_unit::centimeter);
    for (const std::string_view text : {"Inch", "CM", "mm", "inches", ""}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_resolution, [text] {
            (void)detail::parse_resolution_unit(text);
        })) << text;
    }
}

} // namespace
