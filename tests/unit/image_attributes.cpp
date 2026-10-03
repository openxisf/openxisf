// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "model/image_attributes.h"

#include <openxisf/error.h>
#include <openxisf/types.h>

#include "support/throws.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using openxisf::bounds;
using openxisf::color_space;
using openxisf::errc;
using openxisf::geometry;
using openxisf::image_type;
using openxisf::invalid_data_error;
using openxisf::orientation;
using openxisf::pixel_storage;
using openxisf::sample_format;
using openxisf::unsupported_error;
using openxisf::test::throws;
namespace detail = openxisf::detail;

constexpr std::uint64_t max64 = std::numeric_limits<std::uint64_t>::max();

TEST(image_attributes, a_geometry_lists_the_lengths_then_the_number_of_channels)
{
    // Spec §11.5.1: dim1:...:dimN:channel-count, N >= 1.
    EXPECT_EQ(detail::parse_geometry("960:540:3"), (geometry{.dimensions = {960, 540}, .channels = 3}));
    EXPECT_EQ(detail::parse_geometry("7:1"), (geometry{.dimensions = {7}, .channels = 1}));
    EXPECT_EQ(detail::parse_geometry("2:3:4:5:6"), (geometry{.dimensions = {2, 3, 4, 5}, .channels = 6}));
}

TEST(image_attributes, the_items_of_a_geometry_are_unsigned_integers_of_the_plain_text_grammar)
{
    // Spec §8.3: white space around a value is ignored, a sign is allowed, and radix forms are integers too.
    EXPECT_EQ(detail::parse_geometry(" 37 :\t23 : 1 "), (geometry{.dimensions = {37, 23}, .channels = 1}));
    EXPECT_EQ(detail::parse_geometry("+37:0x17:0b1"), (geometry{.dimensions = {37, 23}, .channels = 1}));
    EXPECT_EQ(detail::parse_geometry("18446744073709551615:1"), (geometry{.dimensions = {max64}, .channels = 1}));
}

TEST(image_attributes, a_geometry_without_dimensions_lengths_or_channels_is_invalid)
{
    for (const std::string_view text : {"", "3", ":", "3:", ":3", "3::1", "3:2:", "3;2;1", "3,2,1", "a:1", "3.0:1",
                                        "-1:1", "1e3:1", "3 2:1", "18446744073709551616:1"}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_geometry, [text] { (void)detail::parse_geometry(text); }))
            << text;
    }
}

TEST(image_attributes, every_length_and_the_number_of_channels_are_above_zero)
{
    // Spec §8.5.1: an image with a length of zero is empty, and cannot be serialized.
    for (const std::string_view text : {"0:1", "3:0:1", "3:2:0", "-0:1", "0x0:1"}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_geometry, [text] { (void)detail::parse_geometry(text); }))
            << text;
    }
}

TEST(image_attributes, the_number_of_samples_of_a_geometry_fits_in_64_bits)
{
    // 2^32 x (2^32 - 1) samples fit, 2^32 x 2^32 do not; the channels count as much as the lengths.
    EXPECT_EQ(detail::parse_geometry("4294967296:4294967295").channels, 4294967295U);
    for (const std::string_view text : {"4294967296:4294967296", "4294967296:2:2147483648", "2:18446744073709551615"}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_geometry, [text] { (void)detail::parse_geometry(text); }))
            << text;
    }
}

TEST(image_attributes, the_pixel_data_size_is_the_number_of_samples_times_their_size)
{
    // Spec §8.5.3: n1 x ... x nN x channels x sample size.
    const geometry rgb{.dimensions = {960, 540}, .channels = 3};
    EXPECT_EQ(detail::pixel_data_size(rgb, sample_format::float32), 960U * 540 * 3 * 4);
    EXPECT_EQ(detail::pixel_data_size(rgb, sample_format::complex64), 960U * 540 * 3 * 16);
    // 2^63 samples of a byte fit in 64 bits; of two bytes, they do not.
    const geometry huge{.dimensions = {std::uint64_t{1} << 31U, std::uint64_t{1} << 28U}, .channels = 16};
    EXPECT_EQ(detail::pixel_data_size(huge, sample_format::uint8), std::uint64_t{1} << 63U);
    EXPECT_EQ(detail::pixel_data_size(huge, sample_format::uint16), std::nullopt);
    EXPECT_EQ(detail::pixel_data_size({.dimensions = {max64}, .channels = 1}, sample_format::uint16), std::nullopt);
    EXPECT_EQ(detail::pixel_data_size({.dimensions = {max64 / 2}, .channels = 3}, sample_format::uint8), std::nullopt);
}

TEST(image_attributes, every_sample_format_by_its_name)
{
    // Spec Table 11.
    const std::vector<std::pair<std::string_view, sample_format>> names{
        {"UInt8", sample_format::uint8},         {"UInt16", sample_format::uint16},
        {"UInt32", sample_format::uint32},       {"UInt64", sample_format::uint64},
        {"Float32", sample_format::float32},     {"Float64", sample_format::float64},
        {"Complex32", sample_format::complex32}, {"Complex64", sample_format::complex64}};
    for (const auto& [name, format] : names) {
        EXPECT_EQ(detail::parse_sample_format(name), format) << name;
    }
}

TEST(image_attributes, a_sample_format_outside_the_specification_is_not_supported)
{
    // Names are case-sensitive, and the property types of the same width are not sample formats.
    for (const std::string_view text : {"uint8", "UINT8", " UInt8", "UInt8 ", "Int16", "UInt12", "Float", "Byte", ""}) {
        EXPECT_TRUE(throws<unsupported_error>(errc::unsupported_sample_format, [text] {
            (void)detail::parse_sample_format(text);
        })) << text;
    }
}

TEST(image_attributes, every_colour_space_by_its_name)
{
    // Spec Table 14.
    EXPECT_EQ(detail::parse_color_space("Gray"), color_space::gray);
    EXPECT_EQ(detail::parse_color_space("RGB"), color_space::rgb);
    EXPECT_EQ(detail::parse_color_space("CIELab"), color_space::cie_lab);
    for (const std::string_view text : {"gray", "Grey", "rgb", "CIELAB", "Lab", "CMYK", ""}) {
        EXPECT_TRUE(throws<unsupported_error>(errc::unsupported_color_space, [text] {
            (void)detail::parse_color_space(text);
        })) << text;
    }
}

TEST(image_attributes, a_pixel_storage_is_planar_or_normal)
{
    // Spec Table 13.
    EXPECT_EQ(detail::parse_pixel_storage("Planar"), pixel_storage::planar);
    EXPECT_EQ(detail::parse_pixel_storage("Normal"), pixel_storage::normal);
    for (const std::string_view text : {"planar", "NORMAL", "Interleaved", ""}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_image, [text] {
            (void)detail::parse_pixel_storage(text);
        })) << text;
    }
}

TEST(image_attributes, every_image_type_by_its_name)
{
    // Spec Table 12.
    const std::vector<std::pair<std::string_view, image_type>> names{
        {"Bias", image_type::bias},
        {"Dark", image_type::dark},
        {"Flat", image_type::flat},
        {"Light", image_type::light},
        {"MasterBias", image_type::master_bias},
        {"MasterDark", image_type::master_dark},
        {"MasterFlat", image_type::master_flat},
        {"MasterLight", image_type::master_light},
        {"DefectMap", image_type::defect_map},
        {"RejectionMapHigh", image_type::rejection_map_high},
        {"RejectionMapLow", image_type::rejection_map_low},
        {"BinaryRejectionMapHigh", image_type::binary_rejection_map_high},
        {"BinaryRejectionMapLow", image_type::binary_rejection_map_low},
        {"SlopeMap", image_type::slope_map},
        {"WeightMap", image_type::weight_map}};
    for (const auto& [name, type] : names) {
        EXPECT_EQ(detail::parse_image_type(name), type) << name;
    }
    for (const std::string_view text : {"light", "Science", "Master Dark", ""}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_image, [text] { (void)detail::parse_image_type(text); }))
            << text;
    }
}

TEST(image_attributes, every_orientation_by_its_value)
{
    // Spec §11.5.2.
    const std::vector<std::pair<std::string_view, orientation>> values{{"0", orientation::none},
                                                                       {"flip", orientation::flip},
                                                                       {"90", orientation::rotate_90},
                                                                       {"90;flip", orientation::rotate_90_flip},
                                                                       {"-90", orientation::rotate_minus_90},
                                                                       {"-90;flip", orientation::rotate_minus_90_flip},
                                                                       {"180", orientation::rotate_180},
                                                                       {"180;flip", orientation::rotate_180_flip}};
    for (const auto& [value, turn] : values) {
        EXPECT_EQ(detail::parse_orientation(value), turn) << value;
    }
    for (const std::string_view text : {"270", "-180", "+90", "90; flip", "flip;90", "Flip", "00", ""}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_image, [text] { (void)detail::parse_orientation(text); }))
            << text;
    }
}

TEST(image_attributes, bounds_are_two_finite_floating_point_values_in_increasing_order)
{
    // Spec §11.5.1: lower:upper, each of the plain text grammar of spec §8.3.3.
    EXPECT_EQ(detail::parse_bounds("0:1"), (bounds{.lower = 0.0, .upper = 1.0}));
    EXPECT_EQ(detail::parse_bounds(" -1.5 : 1e3 "), (bounds{.lower = -1.5, .upper = 1000.0}));
    EXPECT_EQ(detail::parse_bounds("0:65535"), (bounds{.lower = 0.0, .upper = 65535.0}));
    for (const std::string_view text :
         {"", "1", ":", "0:", ":1", "0:1:2", "0;1", "a:1", "0:NaN", "-Inf:0", "0:+Inf", "1:0", "1:1", "0:1e999"}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_image, [text] { (void)detail::parse_bounds(text); }))
            << text;
    }
}

TEST(image_attributes, an_offset_is_a_finite_floating_point_value)
{
    // Spec §11.5.2. The caller checks that it is not negative.
    EXPECT_EQ(detail::parse_offset("0"), 0.0);
    EXPECT_EQ(detail::parse_offset(" 100.5 "), 100.5);
    EXPECT_EQ(detail::parse_offset("-1"), -1.0);
    EXPECT_TRUE(std::signbit(detail::parse_offset("-0")));
    for (const std::string_view text : {"", "NaN", "+Inf", "inf", "1,5", "0x10", "1e999", "one"}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_image, [text] { (void)detail::parse_offset(text); }))
            << text;
    }
}

} // namespace
