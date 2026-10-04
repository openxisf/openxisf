// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "core/xoshiro.h"

#include "support/throws.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>

namespace {

using openxisf::errc;
using openxisf::detail::xoshiro256starstar;
using openxisf::test::throws;

static_assert(std::uniform_random_bit_generator<xoshiro256starstar>);

// The first output is rotl(2 × 5, 7) × 9 = 11520, by hand from the definition of the generator. The others were
// computed by an independent implementation of the published algorithm, in Python.
TEST(xoshiro, outputs_from_a_known_state)
{
    xoshiro256starstar generator({1, 2, 3, 4});

    EXPECT_EQ(generator(), 11520U);
    EXPECT_EQ(generator(), 0U);
    EXPECT_EQ(generator(), 0x5A007080U);
    EXPECT_EQ(generator(), 0x10E0000000009D80U);
    EXPECT_EQ(generator(), 0x10E0B61CE1009D80U);
    EXPECT_EQ(generator(), 0x0870021CE143AD00U);
    for (int i = 6; i < 999; ++i) {
        (void)generator();
    }
    EXPECT_EQ(generator(), 0x3039D010986D012DU);
}

TEST(xoshiro, all_zero_state_is_refused)
{
    EXPECT_TRUE(
        throws<openxisf::usage_error>(errc::invalid_argument, [] { xoshiro256starstar generator({0, 0, 0, 0}); }));
}

TEST(xoshiro, generators_from_the_random_device_differ)
{
    xoshiro256starstar first = xoshiro256starstar::from_random_device();
    xoshiro256starstar second = xoshiro256starstar::from_random_device();

    // Two equal outputs from independent 256-bit seeds have a probability of 2^-64.
    EXPECT_NE(first(), second());
}

} // namespace
