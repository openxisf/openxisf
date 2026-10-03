// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "core/xoshiro.h"

#include <openxisf/error.h>

#include <algorithm>
#include <bit>
#include <exception>
#include <new>
#include <random>
#include <string>
#include <system_error>

namespace openxisf::detail {

xoshiro256starstar::xoshiro256starstar(const std::array<std::uint64_t, 4>& state) : state_(state)
{
    // The all-zero state is the one fixed point of the generator.
    if (std::ranges::all_of(state_, [](std::uint64_t word) { return word == 0; })) {
        throw usage_error(errc::invalid_argument, "the state of a xoshiro256** generator must not be all zero");
    }
}

xoshiro256starstar xoshiro256starstar::from_random_device()
{
    // std::random_device reports a failure with an exception type of its own choice, which depends on the standard
    // library. Every one of them becomes an io_error, except std::bad_alloc.
    std::array<std::uint64_t, 4> state{};
    try {
        std::random_device device;
        for (std::uint64_t& word : state) {
            // Each call produces 32 bits.
            word = (std::uint64_t{device()} << 32U) | std::uint64_t{device()};
        }
    } catch (const std::system_error& failure) {
        throw io_error(errc::entropy_unavailable, "no random data for seeding: " + std::string(failure.what()),
                       failure.code());
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& failure) {
        throw io_error(errc::entropy_unavailable, "no random data for seeding: " + std::string(failure.what()));
    }
    if (std::ranges::all_of(state, [](std::uint64_t word) { return word == 0; })) {
        throw io_error(errc::entropy_unavailable, "std::random_device produced only zeros");
    }
    return xoshiro256starstar(state);
}

xoshiro256starstar::result_type xoshiro256starstar::operator()() noexcept
{
    const std::uint64_t result = std::rotl(state_[1] * 5, 7) * 9;
    const std::uint64_t shifted = state_[1] << 17U;
    state_[2] ^= state_[0];
    state_[3] ^= state_[1];
    state_[1] ^= state_[2];
    state_[0] ^= state_[3];
    state_[2] ^= shifted;
    state_[3] = std::rotl(state_[3], 45);
    return result;
}

} // namespace openxisf::detail
