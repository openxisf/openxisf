// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "crypto/sha3.h"

#include <bit>

namespace openxisf::detail {

namespace {

// The round constants of ι (FIPS 202, §3.2.5), one per round of Keccak-f[1600].
constexpr std::array<std::uint64_t, 24> round_constants{
    0x0000000000000001U, 0x0000000000008082U, 0x800000000000808AU, 0x8000000080008000U, 0x000000000000808BU,
    0x0000000080000001U, 0x8000000080008081U, 0x8000000000008009U, 0x000000000000008AU, 0x0000000000000088U,
    0x0000000080008009U, 0x000000008000000AU, 0x000000008000808BU, 0x800000000000008BU, 0x8000000000008089U,
    0x8000000000008003U, 0x8000000000008002U, 0x8000000000000080U, 0x000000000000800AU, 0x800000008000000AU,
    0x8000000080008081U, 0x8000000000008080U, 0x0000000080000001U, 0x8000000080008008U,
};

// The rotation of lane (x, y) by ρ (FIPS 202, §3.2.2), at index x + 5y.
constexpr std::array<int, 25> rotations{
    0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43, 25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14,
};

constexpr std::size_t lane(std::size_t x, std::size_t y) noexcept
{
    return x + (5 * y);
}

// Keccak-f[1600] (FIPS 202, §3.3): 24 rounds of θ, ρ, π, χ and ι.
void permute(std::array<std::uint64_t, 25>& state) noexcept
{
    for (const std::uint64_t round_constant : round_constants) {
        // θ: each lane is combined with the parities of two neighbouring columns.
        std::array<std::uint64_t, 5> parity{};
        for (std::size_t x = 0; x < 5; ++x) {
            parity[x] =
                state[lane(x, 0)] ^ state[lane(x, 1)] ^ state[lane(x, 2)] ^ state[lane(x, 3)] ^ state[lane(x, 4)];
        }
        for (std::size_t x = 0; x < 5; ++x) {
            const std::uint64_t effect = parity[(x + 4) % 5] ^ std::rotl(parity[(x + 1) % 5], 1);
            for (std::size_t y = 0; y < 5; ++y) {
                state[lane(x, y)] ^= effect;
            }
        }

        // ρ rotates each lane, and π moves lane (x, y) to (y, 2x + 3y).
        std::array<std::uint64_t, 25> moved{};
        for (std::size_t x = 0; x < 5; ++x) {
            for (std::size_t y = 0; y < 5; ++y) {
                moved[lane(y, ((2 * x) + (3 * y)) % 5)] = std::rotl(state[lane(x, y)], rotations[lane(x, y)]);
            }
        }

        // χ combines each lane with the next two of its row.
        for (std::size_t y = 0; y < 5; ++y) {
            for (std::size_t x = 0; x < 5; ++x) {
                state[lane(x, y)] = moved[lane(x, y)] ^ (~moved[lane((x + 1) % 5, y)] & moved[lane((x + 2) % 5, y)]);
            }
        }

        // ι.
        state[0] ^= round_constant;
    }
}

} // namespace

sha3::sha3(std::size_t digest_size) noexcept : rate_(200 - (2 * digest_size)) {}

void sha3::update(std::span<const std::byte> data) noexcept
{
    for (const std::byte value : data) {
        absorb(value);
    }
}

void sha3::finish(std::span<std::byte> digest) noexcept
{
    // The SHA-3 domain bits 01 and the first bit of pad10*1, then its last bit at the end of the block (FIPS 202,
    // §6.1 and §5.1). Bits are numbered from the least significant bit of each byte.
    state_[position_ / 8] ^= std::uint64_t{0x06} << (8 * (position_ % 8));
    state_[(rate_ - 1) / 8] ^= std::uint64_t{0x80} << (8 * ((rate_ - 1) % 8));
    permute(state_);
    for (std::size_t i = 0; i < digest.size(); ++i) {
        digest[i] = static_cast<std::byte>(state_[i / 8] >> (8 * (i % 8)));
    }
}

// Lanes hold their bytes in little-endian order (FIPS 202, §B.1), whatever the byte order of the host.
void sha3::absorb(std::byte value) noexcept
{
    state_[position_ / 8] ^= std::to_integer<std::uint64_t>(value) << (8 * (position_ % 8));
    if (++position_ == rate_) {
        permute(state_);
        position_ = 0;
    }
}

} // namespace openxisf::detail
