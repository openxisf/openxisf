// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "model/pixel_layout.h"

#include <openxisf/types.h>

#include "container/block_attributes.h"
#include "support/bytes.h"

#include <gtest/gtest.h>

#include <bit>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using openxisf::pixel_storage;
using openxisf::sample_format;
using openxisf::detail::byte_order;
using openxisf::detail::convert_storage;
using openxisf::detail::to_native_byte_order;
using namespace std::string_view_literals;

// The bytes of value in big-endian order, written out independently of the host.
std::vector<std::byte> big_endian(std::uint64_t value, std::size_t size)
{
    std::vector<std::byte> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[size - 1 - i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFU);
    }
    return bytes;
}

std::vector<std::byte> reversed(std::vector<std::byte> bytes)
{
    return {bytes.rbegin(), bytes.rend()};
}

template <typename T> T load(const std::vector<std::byte>& bytes)
{
    T value{};
    std::memcpy(&value, bytes.data(), sizeof(T));
    return value;
}

TEST(pixel_layout, the_byte_order_of_a_complex_sample_applies_to_each_part)
{
    // Spec §10.4: the byte order is that of the scalars that make up the data.
    EXPECT_EQ(openxisf::detail::byte_order_item_size(sample_format::uint8), 1U);
    EXPECT_EQ(openxisf::detail::byte_order_item_size(sample_format::uint16), 2U);
    EXPECT_EQ(openxisf::detail::byte_order_item_size(sample_format::uint32), 4U);
    EXPECT_EQ(openxisf::detail::byte_order_item_size(sample_format::uint64), 8U);
    EXPECT_EQ(openxisf::detail::byte_order_item_size(sample_format::float32), 4U);
    EXPECT_EQ(openxisf::detail::byte_order_item_size(sample_format::float64), 8U);
    EXPECT_EQ(openxisf::detail::byte_order_item_size(sample_format::complex32), 4U);
    EXPECT_EQ(openxisf::detail::byte_order_item_size(sample_format::complex64), 8U);
}

TEST(pixel_layout, samples_come_to_native_byte_order_from_either_order)
{
    std::vector<std::byte> big = big_endian(0x0102030405060708U, 8);
    std::vector<std::byte> little = reversed(big);
    to_native_byte_order(big, sample_format::uint64, byte_order::big);
    to_native_byte_order(little, sample_format::uint64, byte_order::little);
    EXPECT_EQ(load<std::uint64_t>(big), 0x0102030405060708U);
    EXPECT_EQ(load<std::uint64_t>(little), 0x0102030405060708U);

    // 8-bit samples have no byte order.
    std::vector<std::byte> bytes = openxisf::test::bytes("\x01\x02");
    to_native_byte_order(bytes, sample_format::uint8, byte_order::big);
    EXPECT_EQ(bytes, openxisf::test::bytes("\x01\x02"));
}

TEST(pixel_layout, the_parts_of_a_complex_sample_stay_in_their_order)
{
    // The real part comes first in either byte order; only the bytes of each part are reversed.
    std::vector<std::byte> big = big_endian(std::bit_cast<std::uint32_t>(1.5F), 4);
    const std::vector<std::byte> imag = big_endian(std::bit_cast<std::uint32_t>(-2.25F), 4);
    big.insert(big.end(), imag.begin(), imag.end());
    to_native_byte_order(big, sample_format::complex32, byte_order::big);
    EXPECT_EQ(load<std::complex<float>>(big), std::complex<float>(1.5F, -2.25F));
}

// The sample of channel c at pixel p of a test image: its coordinates in the digits, so that a sample in a wrong place
// shows.
std::uint16_t sample_value(std::size_t c, std::size_t p)
{
    return static_cast<std::uint16_t>((1000 * c) + p);
}

// The samples of an image with the given pixel count and channels, 16-bit, in a storage model: spec §8.5.3 orders the
// pixels by their coordinates, the first varying fastest, which is the order of their index p; the planar model stores
// channel by channel, the normal model pixel by pixel.
std::vector<std::byte> samples(std::size_t pixels, std::size_t channels, pixel_storage storage)
{
    std::vector<std::uint16_t> values(pixels * channels);
    for (std::size_t c = 0; c < channels; ++c) {
        for (std::size_t p = 0; p < pixels; ++p) {
            values[storage == pixel_storage::planar ? (c * pixels) + p : (p * channels) + c] = sample_value(c, p);
        }
    }
    std::vector<std::byte> bytes(values.size() * 2);
    std::memcpy(bytes.data(), values.data(), bytes.size());
    return bytes;
}

TEST(pixel_layout, planar_and_normal_storage_order_the_samples_of_a_two_dimensional_image)
{
    // An RGB image of 3 x 2 pixels, written out: R(0,0) R(1,0) R(2,0) R(0,1) R(1,1) R(2,1) G... in the planar model,
    // and R(0,0) G(0,0) B(0,0) R(1,0) ... in the normal one, where the sample of channel c at (x, y) is 100c + 10y + x.
    const std::vector<std::byte> planar = openxisf::test::bytes("\x00\x01\x02\x0A\x0B\x0C"
                                                                "\x64\x65\x66\x6E\x6F\x70"
                                                                "\xC8\xC9\xCA\xD2\xD3\xD4"sv);
    const std::vector<std::byte> normal = openxisf::test::bytes("\x00\x64\xC8\x01\x65\xC9\x02\x66\xCA"
                                                                "\x0A\x6E\xD2\x0B\x6F\xD3\x0C\x70\xD4"sv);
    std::vector<std::byte> converted(planar.size());
    convert_storage(planar, converted, pixel_storage::planar, 6, 3, 1);
    EXPECT_EQ(converted, normal);
    convert_storage(normal, converted, pixel_storage::normal, 6, 3, 1);
    EXPECT_EQ(converted, planar);
}

TEST(pixel_layout, one_and_three_dimensional_images_have_the_same_order_by_pixel)
{
    // 1-D: 5 pixels of 2 channels. 3-D: 2 x 3 x 2 pixels of 4 channels.
    for (const auto& [pixels, channels] : {std::pair<std::size_t, std::size_t>{5, 2}, {12, 4}}) {
        const std::vector<std::byte> planar = samples(pixels, channels, pixel_storage::planar);
        const std::vector<std::byte> normal = samples(pixels, channels, pixel_storage::normal);
        std::vector<std::byte> converted(planar.size());
        convert_storage(planar, converted, pixel_storage::planar, pixels, channels, 2);
        EXPECT_EQ(converted, normal) << pixels;
        convert_storage(normal, converted, pixel_storage::normal, pixels, channels, 2);
        EXPECT_EQ(converted, planar) << pixels;
    }
}

TEST(pixel_layout, a_single_channel_is_stored_the_same_way_in_both_models)
{
    const std::vector<std::byte> data = openxisf::test::pattern(30);
    std::vector<std::byte> converted(data.size());
    convert_storage(data, converted, pixel_storage::planar, 15, 1, 2);
    EXPECT_EQ(converted, data);
}

TEST(pixel_layout, samples_of_sixteen_bytes_move_as_a_whole)
{
    // Complex64 samples: 3 pixels of 2 channels.
    const std::vector<std::byte> planar = openxisf::test::pattern(std::size_t{3} * 2 * 16);
    std::vector<std::byte> normal(planar.size());
    convert_storage(planar, normal, pixel_storage::planar, 3, 2, 16);
    for (std::size_t p = 0; p < 3; ++p) {
        for (std::size_t c = 0; c < 2; ++c) {
            for (std::size_t b = 0; b < 16; ++b) {
                EXPECT_EQ(normal[(((p * 2) + c) * 16) + b], planar[(((c * 3) + p) * 16) + b]) << p << c << b;
            }
        }
    }
}

} // namespace
