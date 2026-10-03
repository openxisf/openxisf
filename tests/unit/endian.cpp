// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "core/endian.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using openxisf::detail::byte_swap;
using openxisf::detail::load_big_endian;
using openxisf::detail::load_little_endian;
using openxisf::detail::store_big_endian;
using openxisf::detail::store_little_endian;
using openxisf::detail::swap_byte_order;

// The bytes 0, 1, 2, ..., count - 1.
std::vector<std::byte> sequence(std::size_t count)
{
    std::vector<std::byte> bytes(count);
    for (std::size_t i = 0; i < count; ++i) {
        bytes[i] = static_cast<std::byte>(i);
    }
    return bytes;
}

TEST(endian, byte_swap_reverses_the_bytes)
{
    static_assert(byte_swap(std::uint8_t{0x12}) == 0x12);
    static_assert(byte_swap(std::uint16_t{0x1234}) == 0x3412);
    static_assert(byte_swap(std::uint32_t{0x12345678}) == 0x78563412);
    static_assert(byte_swap(std::uint64_t{0x0123456789ABCDEF}) == 0xEFCDAB8967452301);
}

TEST(endian, loads_read_either_byte_order_on_any_host)
{
    const std::array<std::byte, 8> bytes{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
                                         std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08}};
    const std::span<const std::byte, 8> all(bytes);

    EXPECT_EQ(load_little_endian<std::uint16_t>(all.first<2>()), 0x0201);
    EXPECT_EQ(load_big_endian<std::uint16_t>(all.first<2>()), 0x0102);
    EXPECT_EQ(load_little_endian<std::uint32_t>(all.first<4>()), 0x04030201U);
    EXPECT_EQ(load_big_endian<std::uint32_t>(all.first<4>()), 0x01020304U);
    EXPECT_EQ(load_little_endian<std::uint64_t>(all), 0x0807060504030201U);
    EXPECT_EQ(load_big_endian<std::uint64_t>(all), 0x0102030405060708U);
}

TEST(endian, stores_write_either_byte_order_on_any_host)
{
    std::array<std::byte, 4> bytes{};

    store_little_endian(std::span(bytes), std::uint32_t{0x01020304});
    EXPECT_EQ(bytes, (std::array{std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01}}));

    store_big_endian(std::span(bytes), std::uint32_t{0x01020304});
    EXPECT_EQ(bytes, (std::array{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}}));
}

// Item sizes 2, 4 and 8 have their own loops; 16 (128-bit scalars) and 3 take the general one.
class swap_byte_order_items : public testing::TestWithParam<std::size_t>
{};

TEST_P(swap_byte_order_items, reverses_each_item_and_keeps_the_bytes_after_the_last_one)
{
    const std::size_t item_size = GetParam();
    const std::size_t tail = item_size - 1;
    std::vector<std::byte> data = sequence((3 * item_size) + tail);

    swap_byte_order(data, item_size);

    for (std::size_t item = 0; item < 3; ++item) {
        for (std::size_t i = 0; i < item_size; ++i) {
            EXPECT_EQ(data[(item * item_size) + i], static_cast<std::byte>((item * item_size) + item_size - 1 - i))
                << "item " << item << ", byte " << i;
        }
    }
    for (std::size_t i = 3 * item_size; i < data.size(); ++i) {
        EXPECT_EQ(data[i], static_cast<std::byte>(i)) << "tail byte " << i;
    }
}

INSTANTIATE_TEST_SUITE_P(endian, swap_byte_order_items, testing::Values(2, 3, 4, 8, 16));

TEST(endian, swap_byte_order_leaves_one_byte_items_and_empty_data_alone)
{
    std::vector<std::byte> data = sequence(5);
    swap_byte_order(data, 1);
    swap_byte_order(data, 0);
    EXPECT_EQ(data, sequence(5));

    std::vector<std::byte> empty;
    swap_byte_order(empty, 4);
    EXPECT_TRUE(empty.empty());
}

} // namespace
