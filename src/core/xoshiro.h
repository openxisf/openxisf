// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <array>
#include <cstdint>
#include <limits>

namespace openxisf::detail {

/// The xoshiro256** generator of Blackman and Vigna, which the specification recommends for UUIDs and block index
/// identifiers (spec §9.4, §11.5.2). Fast and of high statistical quality; not cryptographically secure, which
/// identifiers do not need. Satisfies std::uniform_random_bit_generator.
class xoshiro256starstar
{
public:
    using result_type = std::uint64_t;

    /// A generator with the given state, which must not be all zero (usage_error).
    explicit xoshiro256starstar(const std::array<std::uint64_t, 4>& state);

    /// A generator seeded with 256 bits from std::random_device. Throws io_error when the operating system provides
    /// no random data.
    [[nodiscard]] static xoshiro256starstar from_random_device();

    result_type operator()() noexcept;

    static constexpr result_type min() noexcept
    {
        return 0;
    }

    static constexpr result_type max() noexcept
    {
        return std::numeric_limits<result_type>::max();
    }

private:
    std::array<std::uint64_t, 4> state_;
};

} // namespace openxisf::detail
