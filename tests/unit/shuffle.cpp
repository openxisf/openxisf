// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Byte shuffling (spec §10.6.2), against the order that the specification defines: byte j of item i of n items moves
// to j * n + i, and the bytes after the last complete item stay where they are.

#include "codec/shuffle.h"

#include <openxisf/error.h>

#include "support/bytes.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

namespace {

using openxisf::errc;
using openxisf::detail::shuffle_bytes;
using openxisf::detail::unshuffle_bytes;
using openxisf::test::bytes;
using openxisf::test::pattern;
using openxisf::test::throws;

std::vector<std::byte> shuffled(const std::vector<std::byte>& input, std::size_t item_size)
{
    std::vector<std::byte> output(input.size());
    shuffle_bytes(input, output, item_size);
    return output;
}

std::vector<std::byte> unshuffled(const std::vector<std::byte>& input, std::size_t item_size)
{
    std::vector<std::byte> output(input.size());
    unshuffle_bytes(input, output, item_size);
    return output;
}

TEST(shuffle, gathers_the_bytes_of_equal_significance)
{
    EXPECT_EQ(shuffled(bytes("a1b2c3"), 2), bytes("abc123"));
    EXPECT_EQ(shuffled(bytes("abcdABCD"), 4), bytes("aAbBcCdD"));
    EXPECT_EQ(unshuffled(bytes("abc123"), 2), bytes("a1b2c3"));
}

TEST(shuffle, keeps_the_bytes_after_the_last_complete_item_in_place)
{
    EXPECT_EQ(shuffled(bytes("abcABCxy"), 3), bytes("aAbBcCxy"));
    EXPECT_EQ(shuffled(bytes("a1b2c3x"), 2), bytes("abc123x"));
    EXPECT_EQ(unshuffled(bytes("abc123x"), 2), bytes("a1b2c3x"));
}

TEST(shuffle, copies_a_block_without_a_complete_item_or_with_items_of_one_byte)
{
    for (const std::size_t item_size : {0U, 1U, 4U, 100U}) {
        EXPECT_EQ(shuffled(bytes("abc"), item_size), bytes("abc")) << item_size;
        EXPECT_EQ(unshuffled(bytes("abc"), item_size), bytes("abc")) << item_size;
    }
    EXPECT_EQ(shuffled(bytes("abcdef"), 1), bytes("abcdef"));
}

TEST(shuffle, needs_an_output_of_the_size_of_the_input)
{
    const std::vector<std::byte> input = bytes("abcd");
    std::vector<std::byte> output(3);
    EXPECT_TRUE(throws<openxisf::usage_error>(errc::invalid_argument, [&] { shuffle_bytes(input, output, 2); }));
    EXPECT_TRUE(throws<openxisf::usage_error>(errc::invalid_argument, [&] { unshuffle_bytes(input, output, 2); }));
}

// Every item size of the sample formats, and the 16 bytes of a Complex64 sample, at lengths around a multiple of 16.
struct shuffle_case
{
    std::size_t item_size = 0;
    std::size_t length = 0;
};

// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const shuffle_case& value, std::ostream* output)
{
    *output << "item" << value.item_size << "_length" << value.length;
}

std::vector<shuffle_case> shuffle_cases()
{
    std::vector<shuffle_case> cases;
    for (const std::size_t item_size : {1U, 2U, 3U, 4U, 8U, 16U}) {
        for (const std::size_t length : {0U, 1U, 15U, 16U, 17U, 100'003U}) {
            cases.push_back({.item_size = item_size, .length = length});
        }
    }
    return cases;
}

class shuffle_layout : public testing::TestWithParam<shuffle_case>
{};

TEST_P(shuffle_layout, follows_the_definition_and_reverses)
{
    const auto [item_size, length] = GetParam();
    const std::vector<std::byte> input = pattern(length);
    const std::size_t items = length / item_size;

    std::vector<std::byte> expected(length);
    for (std::size_t i = 0; i < items; ++i) {
        for (std::size_t j = 0; j < item_size; ++j) {
            expected[(j * items) + i] = input[(i * item_size) + j];
        }
    }
    for (std::size_t k = items * item_size; k < length; ++k) {
        expected[k] = input[k];
    }

    const std::vector<std::byte> output = shuffled(input, item_size);
    EXPECT_EQ(output, expected);
    EXPECT_EQ(unshuffled(output, item_size), input);
}

INSTANTIATE_TEST_SUITE_P(sizes, shuffle_layout, testing::ValuesIn(shuffle_cases()),
                         [](const testing::TestParamInfo<shuffle_case>& parameter) {
                             return "item" + std::to_string(parameter.param.item_size) + "_length" +
                                    std::to_string(parameter.param.length);
                         });

} // namespace
