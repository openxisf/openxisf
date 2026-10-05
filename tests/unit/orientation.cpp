// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Spec §11.5.2: the orientations of an image, applied to show it.

#include <openxisf/error.h>
#include <openxisf/orientation.h>
#include <openxisf/types.h>

#include "support/throws.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <span>
#include <vector>

namespace {

using openxisf::errc;
using openxisf::geometry;
using openxisf::orientation;
using openxisf::pixel_storage;
using openxisf::usage_error;
using openxisf::test::throws;

// The image of every case, 3 × 2, its pixels numbered in storage order:
//
//   0 1 2
//   3 4 5
struct oriented
{
    orientation turn = orientation::none;
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    // The numbers of the pixels of the turned image, in storage order.
    std::vector<std::uint16_t> pixels{};
};

// The name of the orientation, without the semicolon and the minus sign, which CTest names cannot hold.
// NOLINTNEXTLINE(readability-identifier-naming): GoogleTest finds the function by this name.
void PrintTo(const oriented& item, std::ostream* output)
{
    for (const char c : openxisf::orientation_name(item.turn)) {
        *output << (c == ';' ? '_' : (c == '-' ? 'm' : c));
    }
}

class orientation_of_a_3_by_2_image : public testing::TestWithParam<oriented>
{};

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

TEST_P(orientation_of_a_3_by_2_image, one_channel)
{
    const oriented& expected = GetParam();
    const geometry image{.dimensions = {3, 2}, .channels = 1};
    EXPECT_EQ(openxisf::oriented_geometry(image, expected.turn),
              (geometry{.dimensions = {expected.width, expected.height}, .channels = 1}));
    const std::vector<std::byte> turned = openxisf::orient_pixels(
        bytes_of(std::vector<std::uint16_t>{0, 1, 2, 3, 4, 5}), image, 2, pixel_storage::planar, expected.turn);
    EXPECT_EQ(samples_of<std::uint16_t>(turned), expected.pixels);
}

TEST_P(orientation_of_a_3_by_2_image, planar_channels_turn_together)
{
    // Channel 1 holds the numbers plus 10, channel 2 plus 20.
    const oriented& expected = GetParam();
    const geometry image{.dimensions = {3, 2}, .channels = 3};
    std::vector<std::uint16_t> planar;
    std::vector<std::uint16_t> turned;
    for (std::uint16_t channel = 0; channel < 3; ++channel) {
        for (std::uint16_t pixel = 0; pixel < 6; ++pixel) {
            planar.push_back(static_cast<std::uint16_t>((channel * 10) + pixel));
        }
        for (const std::uint16_t pixel : expected.pixels) {
            turned.push_back(static_cast<std::uint16_t>((channel * 10) + pixel));
        }
    }
    EXPECT_EQ(samples_of<std::uint16_t>(
                  openxisf::orient_pixels(bytes_of(planar), image, 2, pixel_storage::planar, expected.turn)),
              turned);
}

TEST_P(orientation_of_a_3_by_2_image, normal_pixels_move_whole)
{
    // Two channels of one byte, interleaved: the number and the number plus 10.
    const oriented& expected = GetParam();
    const geometry image{.dimensions = {3, 2}, .channels = 2};
    // Built at their size and filled: GCC 14 takes the growth of an insert into an empty vector for a write beyond it
    // (-Wstringop-overflow).
    std::vector<std::uint8_t> normal(12);
    std::vector<std::uint8_t> turned(12);
    for (std::size_t i = 0; i < 6; ++i) {
        normal[2 * i] = static_cast<std::uint8_t>(i);
        normal[(2 * i) + 1] = static_cast<std::uint8_t>(i + 10);
        turned[2 * i] = static_cast<std::uint8_t>(expected.pixels[i]);
        turned[(2 * i) + 1] = static_cast<std::uint8_t>(expected.pixels[i] + 10);
    }
    EXPECT_EQ(samples_of<std::uint8_t>(
                  openxisf::orient_pixels(bytes_of(normal), image, 1, pixel_storage::normal, expected.turn)),
              turned);
}

INSTANTIATE_TEST_SUITE_P(
    all, orientation_of_a_3_by_2_image,
    testing::Values(
        oriented{.turn = orientation::none, .width = 3, .height = 2, .pixels = {0, 1, 2, 3, 4, 5}},
        // Flipped horizontally: 2 1 0 / 5 4 3.
        oriented{.turn = orientation::flip, .width = 3, .height = 2, .pixels = {2, 1, 0, 5, 4, 3}},
        // 90 degrees counter-clockwise: the right column becomes the top row.
        oriented{.turn = orientation::rotate_90, .width = 2, .height = 3, .pixels = {2, 5, 1, 4, 0, 3}},
        // Then flipped horizontally.
        oriented{.turn = orientation::rotate_90_flip, .width = 2, .height = 3, .pixels = {5, 2, 4, 1, 3, 0}},
        // 90 degrees clockwise: the left column becomes the top row.
        oriented{.turn = orientation::rotate_minus_90, .width = 2, .height = 3, .pixels = {3, 0, 4, 1, 5, 2}},
        // Then flipped horizontally: the transposed image.
        oriented{.turn = orientation::rotate_minus_90_flip, .width = 2, .height = 3, .pixels = {0, 3, 1, 4, 2, 5}},
        oriented{.turn = orientation::rotate_180, .width = 3, .height = 2, .pixels = {5, 4, 3, 2, 1, 0}},
        // Then flipped horizontally: a vertical flip.
        oriented{.turn = orientation::rotate_180_flip, .width = 3, .height = 2, .pixels = {3, 4, 5, 0, 1, 2}}));

TEST(orientation, only_two_dimensional_images_turn)
{
    const std::vector<std::byte> data(24);
    for (const geometry& shape :
         {geometry{.dimensions = {24}, .channels = 1}, geometry{.dimensions = {2, 3, 4}, .channels = 1}}) {
        EXPECT_TRUE(throws<usage_error>(errc::invalid_argument,
                                        [&] { (void)openxisf::oriented_geometry(shape, orientation::rotate_90); }));
        EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [&] {
            (void)openxisf::orient_pixels(data, shape, 1, pixel_storage::planar, orientation::none);
        }));
    }
}

TEST(orientation, the_pixel_data_must_hold_the_image)
{
    const geometry image{.dimensions = {3, 2}, .channels = 2};
    const std::vector<std::byte> data(24);
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [&] {
        (void)openxisf::orient_pixels(std::span(data).first(23), image, 2, pixel_storage::normal, orientation::flip);
    }));
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [&] {
        (void)openxisf::orient_pixels(data, image, 0, pixel_storage::normal, orientation::flip);
    }));
    EXPECT_EQ(openxisf::orient_pixels(data, image, 2, pixel_storage::normal, orientation::flip).size(), 24U);
}

} // namespace
