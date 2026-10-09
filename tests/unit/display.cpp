// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Spec §8.5.6 and §8.5.7: display functions and the adaptive display function algorithm.

#include <openxisf/display.h>
#include <openxisf/error.h>
#include <openxisf/image.h>
#include <openxisf/types.h>

#include "support/throws.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace {

using openxisf::adaptive_display_options;
using openxisf::display_function;
using openxisf::errc;
using openxisf::image_info;
using openxisf::midtones_transfer;
using openxisf::usage_error;
using openxisf::test::throws;

constexpr double nan = std::numeric_limits<double>::quiet_NaN();

// -----------------------------------------------------------------------------------------------------------------
// Evaluation (spec §8.5.6)

TEST(display, the_midtones_transfer_function_passes_through_its_three_points)
{
    // Equation [5].
    for (const double m : {0.1, 0.25, 0.5, 0.8}) {
        EXPECT_EQ(midtones_transfer(0.0, m), 0.0) << m;
        EXPECT_EQ(midtones_transfer(m, m), 0.5) << m;
        EXPECT_EQ(midtones_transfer(1.0, m), 1.0) << m;
    }
    // (0.1 - 1) 0.25 / ((0.2 - 1) 0.25 - 0.1) = -0.225 / -0.3.
    EXPECT_DOUBLE_EQ(midtones_transfer(0.25, 0.1), 0.75);
    // A balance of 1/2 is linear; below it brightens, above it darkens.
    for (const double x : {0.1, 0.3, 0.7}) {
        EXPECT_DOUBLE_EQ(midtones_transfer(x, 0.5), x) << x;
        EXPECT_GT(midtones_transfer(x, 0.2), x) << x;
        EXPECT_LT(midtones_transfer(x, 0.8), x) << x;
    }
}

TEST(display, the_extremes_of_the_midtones_transfer_function_keep_0_and_1)
{
    // At m = 0 and m = 1 the cases of equation [5] overlap; 0 and 1 keep their values, as in PixInsight.
    EXPECT_EQ(midtones_transfer(0.0, 0.0), 0.0);
    EXPECT_EQ(midtones_transfer(0.3, 0.0), 1.0);
    EXPECT_EQ(midtones_transfer(0.3, 1.0), 0.0);
    EXPECT_EQ(midtones_transfer(1.0, 1.0), 1.0);
}

TEST(display, the_identity_keeps_every_sample)
{
    // Equation [9].
    const display_function identity;
    for (std::size_t component = 0; component < 4; ++component) {
        for (const double x : {0.0, 0.001, 0.25, 0.5, 0.999, 1.0}) {
            EXPECT_DOUBLE_EQ(openxisf::apply_display_function(identity, component, x), x) << component << ' ' << x;
        }
    }
}

TEST(display, clipping_maps_the_range_between_the_clipping_points_to_the_unit_range)
{
    // Equation [6].
    display_function function;
    function.shadows[0] = 0.2;
    function.highlights[0] = 0.6;
    EXPECT_EQ(openxisf::apply_display_function(function, 0, 0.1), 0.0);
    EXPECT_EQ(openxisf::apply_display_function(function, 0, 0.7), 1.0);
    EXPECT_DOUBLE_EQ(openxisf::apply_display_function(function, 0, 0.4), 0.5);
    EXPECT_DOUBLE_EQ(openxisf::apply_display_function(function, 0, 0.5), 0.75);
    // With equal clipping points every sample becomes the clipping point.
    function.shadows[0] = 0.3;
    function.highlights[0] = 0.3;
    for (const double x : {0.0, 0.3, 1.0}) {
        EXPECT_DOUBLE_EQ(openxisf::apply_display_function(function, 0, x), 0.3) << x;
    }
}

TEST(display, expansion_maps_the_unit_range_into_the_expanded_range)
{
    // Equation [7]: (x - l) / (r - l).
    display_function function;
    function.shadows_expansion[1] = -0.5;
    function.highlights_expansion[1] = 1.5;
    EXPECT_DOUBLE_EQ(openxisf::apply_display_function(function, 1, 0.0), 0.25);
    EXPECT_DOUBLE_EQ(openxisf::apply_display_function(function, 1, 1.0), 0.75);
    EXPECT_DOUBLE_EQ(openxisf::apply_display_function(function, 1, 0.5), 0.5);
}

TEST(display, the_display_function_clips_then_transfers_then_expands)
{
    // Equation [8], component by component: only component 2 has these parameters.
    display_function function;
    function.midtones[2] = 0.25;
    function.shadows[2] = 0.1;
    function.highlights[2] = 0.9;
    function.shadows_expansion[2] = -0.1;
    function.highlights_expansion[2] = 1.1;
    // C = (0.5 - 0.1) / 0.8 = 0.5; M(0.5; 0.25) = -0.375 / -0.5 = 0.75; E = (0.75 + 0.1) / 1.2.
    EXPECT_DOUBLE_EQ(openxisf::apply_display_function(function, 2, 0.5), 0.85 / 1.2);
    EXPECT_DOUBLE_EQ(openxisf::apply_display_function(function, 0, 0.5), 0.5);
    EXPECT_DOUBLE_EQ(openxisf::apply_display_function(function, 3, 0.5), 0.5);
    EXPECT_TRUE(
        throws<usage_error>(errc::invalid_argument, [&] { (void)openxisf::apply_display_function(function, 4, 0.5); }));
}

// -----------------------------------------------------------------------------------------------------------------
// The adaptive algorithm (spec §8.5.7)

template <typename T> std::vector<std::byte> bytes_of(const std::vector<T>& samples)
{
    std::vector<std::byte> result(samples.size() * sizeof(T));
    std::memcpy(result.data(), samples.data(), result.size());
    return result;
}

image_info image_of(openxisf::color_space space, std::uint64_t width, std::uint64_t height,
                    openxisf::sample_format format = openxisf::sample_format::float64)
{
    image_info image;
    image.geometry = {.dimensions = {width, height}, .channels = openxisf::nominal_channels(space)};
    image.sample_format = format;
    image.color_space = space;
    if (format == openxisf::sample_format::float32 || format == openxisf::sample_format::float64) {
        image.bounds = openxisf::bounds{.lower = 0.0, .upper = 1.0};
    }
    return image;
}

display_function adaptive_gray(const std::vector<double>& samples, std::uint64_t width, std::uint64_t height)
{
    return openxisf::adaptive_display_function(bytes_of(samples), image_of(openxisf::color_space::gray, width, height));
}

void expect_component(const display_function& function, std::size_t component, double shadows, double highlights,
                      double midtones)
{
    EXPECT_NEAR(function.shadows[component], shadows, 1e-15) << component;
    EXPECT_NEAR(function.highlights[component], highlights, 1e-15) << component;
    EXPECT_NEAR(function.midtones[component], midtones, 1e-15) << component;
    EXPECT_EQ(function.shadows_expansion[component], 0.0) << component;
    EXPECT_EQ(function.highlights_expansion[component], 1.0) << component;
}

void expect_identity(const display_function& function, std::size_t component)
{
    expect_component(function, component, 0.0, 1.0, 0.5);
}

TEST(display, the_adaptive_function_of_a_dark_background)
{
    // Worked by hand, B = 0.25 and C = -2.8. Sorted samples 0.10 0.10 0.11 0.11 0.12 0.12 0.13 0.50 0.90: the median is
    // 0.12. Sorted deviations 0 0 0.01 0.01 0.01 0.02 0.02 0.38 0.78: MADN = 1.4826 × 0.01. The median is below 1/2,
    // so s = 0.12 - 2.8 × 0.014826 = 0.0784872, h = 1, and m = M(0.12 - s; 0.25) = M(0.0415128; 0.25)
    // = -0.75 × 0.0415128 / (-0.5 × 0.0415128 - 0.25) = 0.1149911876505966.
    const display_function function = adaptive_gray({0.10, 0.12, 0.11, 0.13, 0.10, 0.50, 0.11, 0.12, 0.90}, 3, 3);
    expect_component(function, 0, 0.0784872, 1.0, 0.1149911876505966);
    expect_identity(function, 1);
    expect_identity(function, 2);
    expect_identity(function, 3);
}

TEST(display, the_adaptive_function_of_a_bright_background)
{
    // Median 0.7, above 1/2, and MADN = 1.4826 × 0.01: s = 0, h = 0.7 + 2.8 × 0.014826 = 0.7415128, and
    // m = M(0.25; h - 0.7) = M(0.25; 0.0415128) = 0.8850088123494034.
    expect_component(adaptive_gray({0.7, 0.72, 0.69, 0.71, 0.6}, 5, 1), 0, 0.0, 0.7415128, 0.8850088123494034);
}

TEST(display, the_median_of_an_even_count_is_the_mean_of_the_middle_samples)
{
    // Median (0.2 + 0.3) / 2 = 0.25; MADN = 1.4826 × 0.1; s = max(0, 0.25 - 0.415128) = 0; m = M(0.25; 0.25) = 1/2.
    expect_component(adaptive_gray({0.2, 0.4, 0.3, 0.1}, 2, 2), 0, 0.0, 1.0, 0.5);
}

TEST(display, a_channel_without_deviation_is_not_clipped)
{
    // MADN = 0: s = 0 and h = 1; m = M(0.3; 0.25) = -0.225 / -0.4.
    expect_component(adaptive_gray({0.3, 0.3, 0.3}, 3, 1), 0, 0.0, 1.0, 0.5625);
}

TEST(display, samples_beyond_the_white_point_count_as_white)
{
    // Equation [4] maps them to 1: the median is 1 and MADN 0, so s = 0, h = 1 and m = M(0.25; 0) = 1.
    expect_component(adaptive_gray({1.5, 2.0, 3.0}, 3, 1), 0, 0.0, 1.0, 1.0);
}

TEST(display, a_black_channel_gets_a_step_function)
{
    // The median and MADN are 0: s = 0, h = 1 and m = M(0; 0.25) = 0 (equation [14]), which takes 0 to 0 and every
    // sample above it to 1.
    const display_function black = adaptive_gray({0.0, 0.0, 0.0}, 3, 1);
    expect_component(black, 0, 0.0, 1.0, 0.0);
    EXPECT_EQ(openxisf::apply_display_function(black, 0, 0.0), 0.0);
    EXPECT_EQ(openxisf::apply_display_function(black, 0, 1e-6), 1.0);
}

TEST(display, a_median_of_one_half_is_a_dark_background)
{
    // Equation [11]: a_c = 0 when the median is at most 1/2. MADN = 1.4826 × 0.1, s = 0.5 - 0.415128 = 0.084872, h = 1,
    // and m = M(0.415128; 0.25) = 0.6804425173309089.
    expect_component(adaptive_gray({0.4, 0.5, 0.6}, 3, 1), 0, 0.084872, 1.0, 0.6804425173309089);
}

// Three channels of three samples: 0.1 0.2 0.3, 0.4 0.4 0.5 and 0.6 0.7 0.9.
std::vector<double> rgb_planar()
{
    return {0.1, 0.2, 0.3, 0.4, 0.4, 0.5, 0.6, 0.7, 0.9};
}

TEST(display, each_channel_has_its_own_adaptive_function)
{
    // R: median 0.2, MADN 0.14826, s = 0, m = M(0.2; 0.25) = 3/7. G: median 0.4, MADN 0, m = M(0.4; 0.25) = 2/3.
    // B: median 0.7, above 1/2, h = min(1, 0.7 + 0.415128) = 1, m = M(0.25; 0.3) = 0.4375.
    const display_function function =
        openxisf::adaptive_display_function(bytes_of(rgb_planar()), image_of(openxisf::color_space::rgb, 3, 1));
    expect_component(function, 0, 0.0, 1.0, 3.0 / 7.0);
    expect_component(function, 1, 0.0, 1.0, 2.0 / 3.0);
    expect_component(function, 2, 0.0, 1.0, 0.4375);
    expect_identity(function, 3);
}

TEST(display, alpha_channels_do_not_count)
{
    // The image of each_channel_has_its_own_adaptive_function with an alpha channel whose samples would change every
    // statistic, in both storage models.
    const std::vector<double> planar{0.1, 0.2, 0.3, 0.4, 0.4, 0.5, 0.6, 0.7, 0.9, 1.0, 0.0, 1.0};
    image_info with_alpha = image_of(openxisf::color_space::rgb, 3, 1);
    with_alpha.geometry.channels = 4;
    const display_function expected =
        openxisf::adaptive_display_function(bytes_of(rgb_planar()), image_of(openxisf::color_space::rgb, 3, 1));
    EXPECT_EQ(openxisf::adaptive_display_function(bytes_of(planar), with_alpha), expected);

    const std::vector<double> normal{0.1, 0.4, 0.6, 1.0, 0.2, 0.4, 0.7, 0.0, 0.3, 0.5, 0.9, 1.0};
    with_alpha.pixel_storage = openxisf::pixel_storage::normal;
    EXPECT_EQ(openxisf::adaptive_display_function(bytes_of(normal), with_alpha), expected);
    EXPECT_EQ(openxisf::adaptive_display_function(bytes_of(normal), with_alpha, {.linked = true}),
              openxisf::adaptive_display_function(bytes_of(rgb_planar()), image_of(openxisf::color_space::rgb, 3, 1),
                                                  {.linked = true}));
}

TEST(display, linked_channels_share_one_adaptive_function)
{
    // Equations [15] to [18], same image. The smallest median is below 1/2, so a = 0 for every channel, B included:
    // s = (0 + 0 + max(0, 0.7 - 0.415128)) / 3 = 0.09495733..., h = 1, and m = M((0.2 + 0.4 + 0.7) / 3 - s; 0.25).
    const display_function function = openxisf::adaptive_display_function(
        bytes_of(rgb_planar()), image_of(openxisf::color_space::rgb, 3, 1), {.linked = true});
    for (std::size_t component = 0; component < 3; ++component) {
        expect_component(function, component, 0.284872 / 3.0, 1.0, 0.6054133229004647);
    }
    expect_identity(function, 3);
}

TEST(display, linked_channels_of_a_bright_image)
{
    // Every median above 1/2, so a = 1: s = 0, h = (1 + 1 + 0.6 + 2.8 × 1.4826 × 0.02) / 3, and
    // m = M(0.25; h - (0.7 + 0.9 + 0.6) / 3).
    const std::vector<double> bright{0.6, 0.7, 0.8, 0.9, 0.9, 0.9, 0.55, 0.6, 0.62};
    const display_function function = openxisf::adaptive_display_function(
        bytes_of(bright), image_of(openxisf::color_space::rgb, 3, 1), {.linked = true});
    for (std::size_t component = 0; component < 3; ++component) {
        expect_component(function, component, 0.0, 0.8943418666666667, 0.6346298302956855);
    }
}

TEST(display, the_options_change_the_target_and_the_clipping)
{
    // Median 0.12, MADN 0.014826: s = 0.12 - 1.5 × 0.014826, m = M(1.5 × 0.014826; 0.1).
    const display_function function = openxisf::adaptive_display_function(
        bytes_of(std::vector<double>{0.10, 0.12, 0.11, 0.13, 0.10, 0.50, 0.11, 0.12, 0.90}),
        image_of(openxisf::color_space::gray, 9, 1), {.target_background = 0.1, .clipping = -1.5});
    const double x = 1.5 * 0.014826;
    expect_component(function, 0, 0.12 - x, 1.0, -0.9 * x / ((-0.8 * x) - 0.1));
}

TEST(display, storage_and_sample_format_do_not_change_the_statistics)
{
    image_info normal = image_of(openxisf::color_space::rgb, 3, 1);
    normal.pixel_storage = openxisf::pixel_storage::normal;
    const std::vector<double> interleaved{0.1, 0.4, 0.6, 0.2, 0.4, 0.7, 0.3, 0.5, 0.9};
    EXPECT_EQ(openxisf::adaptive_display_function(bytes_of(interleaved), normal),
              openxisf::adaptive_display_function(bytes_of(rgb_planar()), image_of(openxisf::color_space::rgb, 3, 1)));

    // Integer samples are mapped from their representable range: 13107 / 65535 = 0.2, and so on.
    const display_function from_integers = openxisf::adaptive_display_function(
        bytes_of(std::vector<std::uint16_t>{6553, 13107, 19661, 32768}),
        image_of(openxisf::color_space::gray, 4, 1, openxisf::sample_format::uint16));
    const display_function from_reals =
        adaptive_gray({6553.0 / 65535.0, 13107.0 / 65535.0, 19661.0 / 65535.0, 32768.0 / 65535.0}, 4, 1);
    expect_component(from_integers, 0, from_reals.shadows[0], from_reals.highlights[0], from_reals.midtones[0]);

    // Bounds other than [0, 1] map the samples first, clipping those outside.
    image_info wide = image_of(openxisf::color_space::gray, 3, 1);
    wide.bounds = openxisf::bounds{.lower = -1.0, .upper = 3.0};
    const display_function from_wide =
        openxisf::adaptive_display_function(bytes_of(std::vector<double>{-0.6, -5.0, 0.2}), wide);
    const display_function mapped = adaptive_gray({0.1, 0.0, 0.3}, 3, 1);
    expect_component(from_wide, 0, mapped.shadows[0], mapped.highlights[0], mapped.midtones[0]);
}

TEST(display, nan_samples_are_left_out)
{
    const std::vector<float> samples{std::numeric_limits<float>::quiet_NaN(), 0.25F, 0.5F, 0.75F,
                                     std::numeric_limits<float>::quiet_NaN()};
    EXPECT_EQ(openxisf::adaptive_display_function(
                  bytes_of(samples), image_of(openxisf::color_space::gray, 5, 1, openxisf::sample_format::float32)),
              adaptive_gray({0.25, 0.5, 0.75}, 3, 1));
    // A channel of NaN alone keeps the identity.
    const std::vector<double> rgb{nan, nan, nan, 0.4, 0.4, 0.5, 0.6, 0.7, 0.9};
    const display_function separate =
        openxisf::adaptive_display_function(bytes_of(rgb), image_of(openxisf::color_space::rgb, 3, 1));
    expect_identity(separate, 0);
    expect_component(separate, 1, 0.0, 1.0, 2.0 / 3.0);
    // Linked, it does not count: s = (0 + 0.284872) / 2 = 0.142436, h = 1, and m = M(0.55 - s; 0.25).
    const display_function linked = openxisf::adaptive_display_function(
        bytes_of(rgb), image_of(openxisf::color_space::rgb, 3, 1), {.linked = true});
    for (std::size_t component = 0; component < 3; ++component) {
        expect_component(linked, component, 0.142436, 1.0, 0.6736119987130384);
    }
    const std::vector<double> nothing(9, nan);
    const display_function none = openxisf::adaptive_display_function(
        bytes_of(nothing), image_of(openxisf::color_space::rgb, 3, 1), {.linked = true});
    for (std::size_t component = 0; component < 4; ++component) {
        expect_identity(none, component);
    }
}

TEST(display, the_adaptive_function_refuses_what_it_cannot_measure)
{
    const std::vector<std::byte> data(std::size_t{3} * 8);
    const auto refuses = [](std::span<const std::byte> pixels, const image_info& image,
                            const adaptive_display_options& options = {}) {
        return throws<usage_error>(errc::invalid_argument,
                                   [&] { (void)openxisf::adaptive_display_function(pixels, image, options); });
    };
    const image_info gray = image_of(openxisf::color_space::gray, 3, 1);
    EXPECT_TRUE(refuses(std::span(data).first(16), gray)) << "size";
    image_info lab = image_of(openxisf::color_space::cie_lab, 1, 1);
    EXPECT_TRUE(refuses(data, lab));
    image_info flat = gray;
    flat.bounds = openxisf::bounds{.lower = 0.5, .upper = 0.5};
    EXPECT_TRUE(refuses(data, flat));
    image_info infinite = gray;
    infinite.bounds = openxisf::bounds{.lower = 0.0, .upper = std::numeric_limits<double>::infinity()};
    EXPECT_TRUE(refuses(data, infinite));
    // Finite bounds whose width is not: every sample would become 0.
    image_info wide = gray;
    wide.bounds = openxisf::bounds{.lower = -1e308, .upper = 1e308};
    EXPECT_TRUE(refuses(data, wide));
    image_info unbounded = gray;
    unbounded.bounds.reset();
    EXPECT_TRUE(refuses(data, unbounded));
    image_info complex = gray;
    complex.sample_format = openxisf::sample_format::complex32;
    EXPECT_TRUE(refuses(data, complex));
    // An RGB image needs its three nominal channels.
    image_info two_channels = image_of(openxisf::color_space::rgb, 3, 1);
    two_channels.geometry.channels = 2;
    EXPECT_TRUE(refuses(std::vector<std::byte>(std::size_t{2} * 3 * 8), two_channels));
    for (const double background : {-0.1, 1.5, nan}) {
        EXPECT_TRUE(refuses(data, gray, {.target_background = background})) << background;
    }
    for (const double clipping : {0.5, nan, -std::numeric_limits<double>::infinity()}) {
        EXPECT_TRUE(refuses(data, gray, {.clipping = clipping})) << clipping;
    }
}

} // namespace
