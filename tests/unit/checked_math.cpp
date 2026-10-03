// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "core/checked_math.h"

#include "support/throws.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

using openxisf::errc;
using openxisf::invalid_data_error;
using openxisf::detail::checked_add;
using openxisf::detail::checked_cast;
using openxisf::detail::checked_multiply;
using openxisf::test::throws;

constexpr std::uint64_t max64 = std::numeric_limits<std::uint64_t>::max();

TEST(checked_math, add_returns_every_sum_that_fits)
{
    EXPECT_EQ(checked_add<std::uint64_t>(max64 - 1, 1), max64);
    EXPECT_EQ(checked_add<std::uint64_t>(0, max64), max64);
    EXPECT_EQ(checked_add<std::uint32_t>(0xFFFF'FF00U, 0xFFU), 0xFFFF'FFFFU);
}

TEST(checked_math, add_throws_on_overflow)
{
    EXPECT_TRUE(
        throws<invalid_data_error>(errc::arithmetic_overflow, [] { (void)checked_add<std::uint64_t>(max64, 1); }));
    EXPECT_TRUE(throws<invalid_data_error>(errc::arithmetic_overflow,
                                           [] { (void)checked_add<std::uint32_t>(0xFFFF'FF00U, 0x100U); }));
}

TEST(checked_math, multiply_returns_every_product_that_fits)
{
    EXPECT_EQ(checked_multiply<std::uint64_t>(max64 / 3, 3), max64);
    EXPECT_EQ(checked_multiply<std::uint64_t>(0, max64), 0U);
    EXPECT_EQ(checked_multiply<std::uint64_t>(max64, 0), 0U);
    EXPECT_EQ(checked_multiply<std::uint32_t>(65535, 65537), 0xFFFF'FFFFU);
}

TEST(checked_math, multiply_throws_on_overflow)
{
    EXPECT_TRUE(throws<invalid_data_error>(errc::arithmetic_overflow, [] {
        (void)checked_multiply<std::uint64_t>(std::uint64_t{1} << 32, std::uint64_t{1} << 32);
    }));
    EXPECT_TRUE(throws<invalid_data_error>(errc::arithmetic_overflow,
                                           [] { (void)checked_multiply<std::uint64_t>(max64 / 3 + 1, 3); }));
    EXPECT_TRUE(throws<invalid_data_error>(errc::arithmetic_overflow,
                                           [] { (void)checked_multiply<std::uint32_t>(65536, 65536); }));
}

TEST(checked_math, cast_returns_every_value_the_target_represents)
{
    EXPECT_EQ(checked_cast<std::uint8_t>(255), 255);
    EXPECT_EQ(checked_cast<std::int8_t>(-128), -128);
    EXPECT_EQ(checked_cast<std::size_t>(max64), max64);
    EXPECT_EQ(checked_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
              std::uint64_t{std::numeric_limits<std::int64_t>::max()});
}

TEST(checked_math, cast_throws_when_the_target_cannot_represent_the_value)
{
    EXPECT_TRUE(throws<invalid_data_error>(errc::arithmetic_overflow, [] { (void)checked_cast<std::uint8_t>(256); }));
    EXPECT_TRUE(throws<invalid_data_error>(errc::arithmetic_overflow, [] { (void)checked_cast<std::int8_t>(-129); }));
    EXPECT_TRUE(throws<invalid_data_error>(errc::arithmetic_overflow, [] { (void)checked_cast<std::uint64_t>(-1); }));
    EXPECT_TRUE(throws<invalid_data_error>(errc::arithmetic_overflow, [] { (void)checked_cast<std::int64_t>(max64); }));
}

} // namespace
