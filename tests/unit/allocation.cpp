// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "core/allocation.h"

#include "support/throws.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <new>

namespace {

using openxisf::errc;
using openxisf::detail::check_allocation;
using openxisf::test::throws;

constexpr std::uint64_t max64 = std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t largest = std::numeric_limits<std::int64_t>::max();

TEST(allocation, the_limit_bounds_an_allocation)
{
    EXPECT_NO_THROW(check_allocation(1000, {.max_allocation = 1000}, "the data have"));
    EXPECT_TRUE(throws<openxisf::limit_error>(
        errc::allocation_too_large, [] { check_allocation(1001, {.max_allocation = 1000}, "the data have"); }));
    EXPECT_TRUE(throws<openxisf::limit_error>(errc::allocation_too_large,
                                              [] { check_allocation(max64, {.max_allocation = 1000}, "the data"); }));
}

TEST(allocation, without_a_limit_a_size_beyond_any_allocation_fails_as_an_allocation)
{
    // A std::vector would throw std::length_error, which no function of the library may throw.
    EXPECT_NO_THROW(check_allocation(largest, {.max_allocation = 0}, "the data have"));
    EXPECT_THROW(check_allocation(largest + 1, {.max_allocation = 0}, "the data have"), std::bad_alloc);
    EXPECT_THROW(check_allocation(max64, {.max_allocation = 0}, "the data have"), std::bad_alloc);
}

} // namespace
