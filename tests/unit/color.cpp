// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Annex B and spec §8.5.4.1: luminance coefficients, the colour transformations, and the conversion of the pixel data
// of CIE L*a*b* images.

#include <openxisf/color.h>
#include <openxisf/error.h>
#include <openxisf/image.h>
#include <openxisf/io.h>
#include <openxisf/types.h>

#include "support/bytes.h"
#include "support/opened_unit.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using openxisf::color_components;
using openxisf::color_converter;
using openxisf::errc;
using openxisf::image_info;
using openxisf::rgb_working_space;
using openxisf::usage_error;
using openxisf::test::throws;

// The examples of spec §8.5.4.1 and §11.8.2, relative to D50.
const std::array<double, 3> srgb_x{0.648431, 0.321152, 0.155886};
const std::array<double, 3> srgb_y{0.330856, 0.597871, 0.066044};
const std::array<double, 3> adobe_x{0.648431, 0.230154, 0.155886};
const std::array<double, 3> adobe_y{0.330856, 0.701572, 0.066044};

rgb_working_space adobe_rgb()
{
    return {.gamma = 2.2, .x = adobe_x, .y = adobe_y, .luminance = {0.311114, 0.625662, 0.063224}, .name = {}};
}

// The working spaces of tests/data/color/annex_b.py, by the names it gives them.
std::map<std::string, rgb_working_space> reference_spaces()
{
    rgb_working_space linear;
    linear.gamma = 1.0;
    rgb_working_space prophoto;
    prophoto.gamma = 1.8;
    prophoto.x = {0.7347, 0.1596, 0.0366};
    prophoto.y = {0.2653, 0.8404, 0.0001};
    return {
        {"srgb", rgb_working_space{}}, {"linear-srgb", linear}, {"adobe-rgb", adobe_rgb()}, {"prophoto-rgb", prophoto}};
}

std::string reference_text()
{
    const openxisf::file_source source(std::string(OPENXISF_TEST_DATA_DIR) + "/color/annex_b.txt");
    std::vector<std::byte> data(source.size());
    source.read(0, data);
    return openxisf::test::text(data);
}

color_components read_triplet(std::istream& line)
{
    color_components values{};
    line >> values[0] >> values[1] >> values[2];
    return values;
}

void expect_near(const color_components& actual, const color_components& expected, double tolerance)
{
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(actual[i], expected[i], tolerance) << "component " << i;
    }
}

// -----------------------------------------------------------------------------------------------------------------
// Luminance coefficients (spec §8.5.4.1)

TEST(color, the_luminance_coefficients_follow_from_the_chromaticities_and_d50)
{
    // The examples of spec §11.8.2, written with six decimals.
    const std::optional<std::array<double, 3>> srgb_derived = openxisf::luminance_coefficients(srgb_x, srgb_y);
    const std::optional<std::array<double, 3>> adobe_derived = openxisf::luminance_coefficients(adobe_x, adobe_y);
    const std::array<double, 3>& srgb = openxisf::test::value_of(srgb_derived);
    const std::array<double, 3>& adobe = openxisf::test::value_of(adobe_derived);
    const std::array<double, 3> srgb_luminance{0.222491, 0.716888, 0.060621};
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(srgb[i], srgb_luminance[i], 1e-6) << i;
        EXPECT_NEAR(adobe[i], adobe_rgb().luminance[i], 1e-6) << i;
    }
    EXPECT_NEAR(srgb[0] + srgb[1] + srgb[2], 1.0, 1e-12);
}

TEST(color, chromaticities_without_a_solution_define_no_working_space)
{
    // A y coordinate of zero, and three equal primaries, whose system is singular.
    EXPECT_EQ(openxisf::luminance_coefficients({0.6, 0.3, 0.15}, {0.3, 0.6, 0.0}), std::nullopt);
    EXPECT_EQ(openxisf::luminance_coefficients({0.3, 0.3, 0.3}, {0.3, 0.3, 0.3}), std::nullopt);
    // Two equal primaries, whose determinant is a residue of rounding where multiply-adds are fused, and three on the
    // line x + y = 0.8, whose rounded elements leave one everywhere.
    EXPECT_EQ(openxisf::luminance_coefficients({0.64, 0.64, 0.15}, {0.33, 0.33, 0.06}), std::nullopt);
    EXPECT_EQ(openxisf::luminance_coefficients({0.2, 0.4, 0.6}, {0.6, 0.4, 0.2}), std::nullopt);
}

// -----------------------------------------------------------------------------------------------------------------
// The transformations (Annex B)

TEST(color, the_transformations_agree_with_an_independent_computation)
{
    // tests/data/color/annex_b.txt, written by annex_b.py in decimal arithmetic of 50 digits.
    const std::map<std::string, rgb_working_space> spaces = reference_spaces();
    std::istringstream lines(reference_text());
    std::size_t count = 0;
    for (std::string line; std::getline(lines, line);) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        SCOPED_TRACE(line);
        std::istringstream fields(line);
        std::string space;
        std::string input;
        fields >> space >> input;
        const color_converter converter(spaces.at(space));
        const color_components given = read_triplet(fields);
        std::string label;
        if (input == "rgb") {
            fields >> label;
            const color_components xyz = read_triplet(fields);
            fields >> label;
            const color_components lab = read_triplet(fields);
            double gray = 0.0;
            fields >> label >> gray;
            expect_near(converter.rgb_to_xyz(given), xyz, 1e-13);
            expect_near(openxisf::xyz_to_lab(xyz), lab, 1e-13);
            expect_near(converter.rgb_to_lab(given), lab, 1e-13);
            EXPECT_NEAR(converter.grayscale(given), gray, 1e-13);
        } else {
            fields >> label;
            const color_components rgb = read_triplet(fields);
            expect_near(converter.lab_to_rgb(given), rgb, 1e-12);
        }
        ASSERT_FALSE(fields.fail());
        ++count;
    }
    EXPECT_EQ(count, 100U);
}

TEST(color, white_becomes_the_reference_white_and_back)
{
    // The lightness of the reference white is exactly 1, as that of black is 0: 1.16 - 0.16 rounds below 1.
    EXPECT_EQ(openxisf::xyz_to_lab({0.96422, 1.0, 0.82521}), (color_components{1.0, 0.5, 0.5}));
    // Annex B.1: M takes the linear RGB white point (1, 1, 1) to D50, for every working space.
    for (const auto& [name, space] : reference_spaces()) {
        SCOPED_TRACE(name);
        const color_converter converter(space);
        expect_near(converter.rgb_to_xyz({1.0, 1.0, 1.0}), {0.96422, 1.0, 0.82521}, 1e-15);
        expect_near(converter.rgb_to_lab({1.0, 1.0, 1.0}), {1.0, 0.5, 0.5}, 1e-15);
        expect_near(converter.lab_to_rgb({1.0, 0.5, 0.5}), {1.0, 1.0, 1.0}, 1e-15);
        const std::array<color_components, 3>& m = converter.rgb_to_xyz_matrix();
        EXPECT_NEAR(m[1][0] + m[1][1] + m[1][2], 1.0, 1e-15);
    }
}

TEST(color, the_srgb_functions_have_a_linear_part)
{
    // Equations [49] and [55], at and around their thresholds.
    const color_converter srgb;
    EXPECT_DOUBLE_EQ(srgb.linearize(0.04045), 0.04045 / 12.92);
    EXPECT_DOUBLE_EQ(srgb.linearize(0.5), std::pow((0.5 + 0.055) / 1.055, 2.4));
    EXPECT_DOUBLE_EQ(srgb.delinearize(0.0031308), 12.92 * 0.0031308);
    EXPECT_DOUBLE_EQ(srgb.delinearize(0.5), (1.055 * std::pow(0.5, 1.0 / 2.4)) - 0.055);
    // Black and white keep their values exactly, as they do with a gamma.
    EXPECT_EQ(srgb.linearize(0.0), 0.0);
    EXPECT_EQ(srgb.delinearize(0.0), 0.0);
    EXPECT_EQ(srgb.linearize(1.0), 1.0);
    EXPECT_EQ(srgb.delinearize(1.0), 1.0);
    // Each is the inverse of the other.
    for (const double value : {0.0, 0.001, 0.04, 0.0405, 0.3, 0.9, 1.0}) {
        EXPECT_NEAR(srgb.delinearize(srgb.linearize(value)), value, 1e-15) << value;
    }
}

TEST(color, a_gamma_is_an_exponent)
{
    const color_converter adobe(adobe_rgb());
    EXPECT_DOUBLE_EQ(adobe.linearize(0.5), std::pow(0.5, 2.2));
    EXPECT_DOUBLE_EQ(adobe.delinearize(0.5), std::pow(0.5, 1.0 / 2.2));
}

TEST(color, nominal_components_are_clipped_to_the_unit_range)
{
    // Annex B: components are in [0, 1], and the results of the transformations are clipped to it.
    const color_converter adobe(adobe_rgb());
    EXPECT_EQ(adobe.rgb_to_lab({-0.5, 1.5, 0.25}), adobe.rgb_to_lab({0.0, 1.0, 0.25}));
    EXPECT_EQ(adobe.grayscale({2.0, 2.0, 2.0}), adobe.grayscale({1.0, 1.0, 1.0}));
    EXPECT_EQ(adobe.lab_to_rgb({2.0, -0.5, 0.5}), adobe.lab_to_rgb({1.0, 0.0, 0.5}));
    EXPECT_EQ(openxisf::lab_to_xyz({-1.0, 0.5, 3.0}), openxisf::lab_to_xyz({0.0, 0.5, 1.0}));
    EXPECT_EQ(adobe.linearize(-1.0), 0.0);
    EXPECT_EQ(adobe.delinearize(4.0), 1.0);
    // A colour outside the gamut: tristimulus values are not clipped, RGB components are.
    const color_components xyz = openxisf::lab_to_xyz({0.5, 1.0, 0.0});
    EXPECT_GT(xyz[2], 0.82521);
    for (const double component : adobe.xyz_to_rgb(xyz)) {
        EXPECT_GE(component, 0.0);
        EXPECT_LE(component, 1.0);
    }
    for (const double component : openxisf::xyz_to_lab({-1.0, 5.0, 3.0})) {
        EXPECT_GE(component, 0.0);
        EXPECT_LE(component, 1.0);
    }
}

TEST(color, in_gamut_colours_go_to_cie_lab_and_back)
{
    const color_converter srgb;
    const color_converter adobe(adobe_rgb());
    for (const color_components rgb : {color_components{0.2, 0.4, 0.6}, color_components{0.9, 0.1, 0.3},
                                       color_components{0.001, 0.0005, 0.002}, color_components{0.0, 1.0, 0.0}}) {
        expect_near(srgb.lab_to_rgb(srgb.rgb_to_lab(rgb)), rgb, 1e-12);
        expect_near(srgb.xyz_to_rgb(srgb.rgb_to_xyz(rgb)), rgb, 1e-12);
        // A component of zero comes back as the 1/γ-th power of the rounding error, about 3e-8 for γ = 2.2.
        expect_near(adobe.lab_to_rgb(adobe.rgb_to_lab(rgb)), rgb, 1e-7);
    }
}

TEST(color, black_is_exact)
{
    // The linear parts of equations [56] and [61] give exactly 0 for black, in every working space.
    for (const auto& [name, space] : reference_spaces()) {
        SCOPED_TRACE(name);
        const color_converter converter(space);
        EXPECT_EQ(converter.rgb_to_lab({0.0, 0.0, 0.0}), (color_components{0.0, 0.5, 0.5}));
        EXPECT_EQ(converter.lab_to_rgb({0.0, 0.5, 0.5}), (color_components{0.0, 0.0, 0.0}));
        EXPECT_EQ(converter.grayscale({0.0, 0.0, 0.0}), 0.0);
    }
}

TEST(color, the_grayscale_component_is_the_lightness)
{
    // Equation [64]: L of CIE L*a*b*, which depends on CIE Y alone.
    const color_converter adobe(adobe_rgb());
    for (const color_components rgb : {color_components{0.2, 0.4, 0.6}, color_components{0.9, 0.1, 0.3}}) {
        EXPECT_NEAR(adobe.grayscale(rgb), adobe.rgb_to_lab(rgb)[0], 1e-15);
    }
}

TEST(color, a_working_space_needs_valid_chromaticities_and_gamma)
{
    for (const double gamma :
         {0.0, -2.2, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        rgb_working_space space;
        space.gamma = gamma;
        EXPECT_TRUE(throws<usage_error>(errc::invalid_rgb_working_space, [&] { (void)color_converter(space); }))
            << gamma;
    }
    rgb_working_space singular;
    singular.x = {0.3, 0.3, 0.3};
    singular.y = {0.3, 0.3, 0.3};
    EXPECT_TRUE(throws<usage_error>(errc::invalid_rgb_working_space, [&] { (void)color_converter(singular); }));
    // Spec §8.5.4.1: chromaticity coordinates are in [0, 1].
    rgb_working_space beyond;
    beyond.x[0] = 1.2;
    EXPECT_TRUE(throws<usage_error>(errc::invalid_rgb_working_space, [&] { (void)color_converter(beyond); }));
}

TEST(color, the_luminance_member_is_not_used)
{
    // Annex B uses the coefficients derived from the chromaticities.
    rgb_working_space rounded = adobe_rgb();
    rounded.luminance = {0.3, 0.6, 0.1};
    EXPECT_EQ(color_converter(rounded).rgb_to_xyz_matrix(), color_converter(adobe_rgb()).rgb_to_xyz_matrix());
}

// -----------------------------------------------------------------------------------------------------------------
// Pixel data of CIE L*a*b* images (spec §8.5.4.1)

template <typename T> std::vector<std::byte> bytes_of(const std::vector<T>& samples)
{
    std::vector<std::byte> result(samples.size() * sizeof(T));
    std::memcpy(result.data(), samples.data(), result.size());
    return result;
}

template <typename T> std::vector<T> samples_of(std::span<const std::byte> data)
{
    std::vector<T> result(data.size() / sizeof(T));
    std::memcpy(result.data(), data.data(), data.size());
    return result;
}

image_info lab_image(openxisf::sample_format format, std::uint64_t channels, openxisf::pixel_storage storage)
{
    image_info image;
    image.geometry = {.dimensions = {2, 1}, .channels = channels};
    image.sample_format = format;
    image.color_space = openxisf::color_space::cie_lab;
    image.pixel_storage = storage;
    return image;
}

TEST(color, pixel_data_go_to_cie_lab_and_back_through_the_representable_range)
{
    // Two pixels with an alpha channel, UInt16, planar: R of both pixels, then G, B and alpha.
    const image_info image = lab_image(openxisf::sample_format::uint16, 4, openxisf::pixel_storage::planar);
    const std::vector<std::uint16_t> rgb{65535, 13107, 0, 26214, 32768, 39321, 7, 9};
    std::vector<std::byte> data = bytes_of(rgb);
    openxisf::convert_rgb_to_lab(data, image);
    const std::vector<std::uint16_t> lab = samples_of<std::uint16_t>(data);

    const color_converter srgb;
    const std::array<color_components, 2> expected{srgb.rgb_to_lab({1.0, 0.0, 32768.0 / 65535.0}),
                                                   srgb.rgb_to_lab({0.2, 0.4, 0.6})};
    for (std::size_t pixel = 0; pixel < 2; ++pixel) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            EXPECT_EQ(lab[(channel * 2) + pixel], std::lround(expected[pixel][channel] * 65535.0))
                << pixel << ' ' << channel;
        }
    }
    EXPECT_EQ(lab[6], 7);
    EXPECT_EQ(lab[7], 9);

    // Back, from the stored components; 16-bit L*a*b* components do not hold every RGB colour exactly.
    openxisf::convert_lab_to_rgb(data, image);
    const std::vector<std::uint16_t> back = samples_of<std::uint16_t>(data);
    for (std::size_t pixel = 0; pixel < 2; ++pixel) {
        const color_components components{lab[pixel] / 65535.0, lab[2 + pixel] / 65535.0, lab[4 + pixel] / 65535.0};
        const color_components expected_rgb = srgb.lab_to_rgb(components);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            EXPECT_EQ(back[(channel * 2) + pixel], std::lround(expected_rgb[channel] * 65535.0))
                << pixel << ' ' << channel;
            EXPECT_NEAR(back[(channel * 2) + pixel], rgb[(channel * 2) + pixel], 16) << pixel << ' ' << channel;
        }
    }
    EXPECT_EQ(back[6], 7);
    EXPECT_EQ(back[7], 9);
}

TEST(color, pixel_data_use_the_working_space_bounds_and_storage_of_the_image)
{
    // Float64, normal storage, bounds [-1, 3], Adobe RGB (1998).
    image_info image = lab_image(openxisf::sample_format::float64, 3, openxisf::pixel_storage::normal);
    image.bounds = openxisf::bounds{.lower = -1.0, .upper = 3.0};
    image.rgb_working_space = adobe_rgb();
    const std::vector<double> rgb{-1.0, 0.6, 1.4, 1.8, -5.0, 0.2};
    std::vector<std::byte> data = bytes_of(rgb);
    openxisf::convert_rgb_to_lab(data, image);
    const std::vector<double> lab = samples_of<double>(data);

    const color_converter adobe(adobe_rgb());
    const std::array<color_components, 2> expected{adobe.rgb_to_lab({0.0, 0.4, 0.6}),
                                                   adobe.rgb_to_lab({0.7, 0.0, 0.3})};
    for (std::size_t pixel = 0; pixel < 2; ++pixel) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            EXPECT_NEAR(lab[(pixel * 3) + channel], -1.0 + (4.0 * expected[pixel][channel]), 1e-14)
                << pixel << ' ' << channel;
        }
    }
    openxisf::convert_lab_to_rgb(data, image);
    const std::vector<double> back = samples_of<double>(data);
    // The sample below the black point comes back at it. A component of 0 comes back as the 1/γ-th power of the
    // rounding error, which depends on the platform's pow(): about 1e-7 for γ = 2.2, 4e-7 in this range of 4.
    const std::vector<double> clipped{-1.0, 0.6, 1.4, 1.8, -1.0, 0.2};
    for (std::size_t i = 0; i < rgb.size(); ++i) {
        EXPECT_NEAR(back[i], clipped[i], 1e-6) << i;
    }
}

// A component in [0, 1] as a sample of type T: through the representable range of an integer type, whose largest value
// is white, or as it is.
template <typename T> T sample_of(double component)
{
    if constexpr (std::is_floating_point_v<T>) {
        return static_cast<T>(component);
    } else {
        constexpr auto largest = std::numeric_limits<T>::max();
        return component >= 1.0 ? largest : static_cast<T>(std::round(component * static_cast<double>(largest)));
    }
}

// The component that a sample of type T stands for, as the conversion reads it.
template <typename T> double component_of(T sample)
{
    if constexpr (std::is_floating_point_v<T>) {
        return static_cast<double>(sample);
    } else {
        return static_cast<double>(sample) / static_cast<double>(std::numeric_limits<T>::max());
    }
}

TEST(color, pixel_data_of_the_other_real_formats_go_to_cie_lab_and_back)
{
    // UInt32, UInt64 and Float32, beside the UInt8, UInt16 and Float64 of the tests above; normal storage. The
    // tolerances are the precision of each format, with room for the rounding of the conversion.
    const color_converter srgb;
    const std::array<color_components, 2> rgb{{{1.0, 0.25, 0.0}, {0.2, 0.4, 0.6}}};
    const auto check = [&]<typename T>(std::type_identity<T>, openxisf::sample_format format, double tolerance) {
        SCOPED_TRACE(std::string(openxisf::sample_format_name(format)));
        image_info image = lab_image(format, 3, openxisf::pixel_storage::normal);
        if constexpr (std::is_floating_point_v<T>) {
            image.bounds = openxisf::bounds{.lower = 0.0, .upper = 1.0};
        }
        std::vector<T> samples(6);
        for (std::size_t i = 0; i < 6; ++i) {
            samples[i] = sample_of<T>(rgb[i / 3][i % 3]);
        }
        std::vector<std::byte> data = bytes_of(samples);
        openxisf::convert_rgb_to_lab(data, image);
        const std::vector<T> lab = samples_of<T>(data);
        for (std::size_t pixel = 0; pixel < 2; ++pixel) {
            const color_components given{component_of(samples[pixel * 3]), component_of(samples[(pixel * 3) + 1]),
                                         component_of(samples[(pixel * 3) + 2])};
            const color_components expected = srgb.rgb_to_lab(given);
            for (std::size_t channel = 0; channel < 3; ++channel) {
                EXPECT_NEAR(component_of(lab[(pixel * 3) + channel]), expected[channel], tolerance)
                    << pixel << ' ' << channel;
            }
        }
        openxisf::convert_lab_to_rgb(data, image);
        const std::vector<T> back = samples_of<T>(data);
        for (std::size_t i = 0; i < 6; ++i) {
            EXPECT_NEAR(component_of(back[i]), component_of(samples[i]), tolerance) << i;
        }
    };
    check(std::type_identity<std::uint32_t>{}, openxisf::sample_format::uint32, 1e-8);
    check(std::type_identity<std::uint64_t>{}, openxisf::sample_format::uint64, 1e-12);
    check(std::type_identity<float>{}, openxisf::sample_format::float32, 1e-6);
}

TEST(color, integer_samples_are_rounded_and_kept_in_their_range)
{
    // UInt8 with bounds beyond its range: the results stay within 0 and 255.
    image_info image = lab_image(openxisf::sample_format::uint8, 3, openxisf::pixel_storage::normal);
    image.bounds = openxisf::bounds{.lower = -100.0, .upper = 400.0};
    std::vector<std::byte> data = bytes_of(std::vector<std::uint8_t>{255, 255, 255, 0, 0, 0});
    openxisf::convert_lab_to_rgb(data, image);
    // (255 + 100) / 500 = 0.71 for each component of the first pixel, and 0.2 for those of the second.
    const std::vector<std::uint8_t> rgb = samples_of<std::uint8_t>(data);
    const color_converter srgb;
    const color_components first = srgb.lab_to_rgb({0.71, 0.71, 0.71});
    const color_components second = srgb.lab_to_rgb({0.2, 0.2, 0.2});
    const auto stored = [](double component) {
        return std::clamp(std::lround(-100.0 + (500.0 * component)), 0L, 255L);
    };
    for (std::size_t channel = 0; channel < 3; ++channel) {
        EXPECT_EQ(rgb[channel], stored(first[channel])) << channel;
        EXPECT_EQ(rgb[3 + channel], stored(second[channel])) << channel;
    }
}

TEST(color, pixel_conversion_refuses_what_it_cannot_convert)
{
    using openxisf::sample_format;
    const auto refuses = [](const image_info& image, std::size_t size, errc code = errc::invalid_argument) {
        std::vector<std::byte> data(size);
        return throws<usage_error>(code, [&] { openxisf::convert_rgb_to_lab(data, image); }) &&
               throws<usage_error>(code, [&] { openxisf::convert_lab_to_rgb(data, image); });
    };
    image_info rgb = lab_image(sample_format::uint32, 3, openxisf::pixel_storage::planar);
    rgb.color_space = openxisf::color_space::rgb;
    EXPECT_TRUE(refuses(rgb, 24));
    EXPECT_TRUE(refuses(lab_image(sample_format::uint32, 2, openxisf::pixel_storage::planar), 16)) << "two channels";
    EXPECT_TRUE(refuses(lab_image(sample_format::float32, 3, openxisf::pixel_storage::planar), 24)) << "no bounds";
    EXPECT_TRUE(refuses(lab_image(sample_format::uint16, 3, openxisf::pixel_storage::planar), 24)) << "size";
    image_info complex = lab_image(sample_format::complex32, 3, openxisf::pixel_storage::planar);
    complex.bounds = openxisf::bounds{};
    EXPECT_TRUE(refuses(complex, 48));
    image_info reversed = lab_image(sample_format::float32, 3, openxisf::pixel_storage::planar);
    reversed.bounds = openxisf::bounds{.lower = 1.0, .upper = 0.0};
    EXPECT_TRUE(refuses(reversed, 24));
    image_info flat = reversed;
    flat.bounds = openxisf::bounds{.lower = 0.5, .upper = 0.5};
    EXPECT_TRUE(refuses(flat, 24));
    image_info infinite = reversed;
    infinite.bounds = openxisf::bounds{.lower = -std::numeric_limits<double>::infinity(), .upper = 1.0};
    EXPECT_TRUE(refuses(infinite, 24));
    // Finite bounds whose width is not: black would become NaN and infinities.
    image_info wide = reversed;
    wide.bounds = openxisf::bounds{.lower = -1e308, .upper = 1e308};
    EXPECT_TRUE(refuses(wide, 24));
    image_info singular = lab_image(sample_format::uint32, 3, openxisf::pixel_storage::planar);
    singular.rgb_working_space = rgb_working_space{};
    singular.rgb_working_space->x = {0.3, 0.3, 0.3};
    singular.rgb_working_space->y = {0.3, 0.3, 0.3};
    EXPECT_TRUE(refuses(singular, 24, errc::invalid_rgb_working_space));
}

} // namespace
