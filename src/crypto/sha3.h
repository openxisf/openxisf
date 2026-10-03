// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace openxisf::detail {

/// SHA3-256 and SHA3-512 (FIPS 202, §6.1): the Keccak-f[1600] sponge with the SHA-3 padding, and a capacity of twice
/// the digest size.
class sha3
{
public:
    /// digest_size is 32 for SHA3-256 and 64 for SHA3-512.
    explicit sha3(std::size_t digest_size) noexcept;

    void update(std::span<const std::byte> data) noexcept;

    /// Writes the digest of the message, which has the size given to the constructor. The object cannot be used
    /// afterwards.
    void finish(std::span<std::byte> digest) noexcept;

private:
    void absorb(std::byte value) noexcept;

    /// The 25 lanes of the state, lane (x, y) at index x + 5y (FIPS 202, §3.1.2).
    std::array<std::uint64_t, 25> state_{};
    /// The bytes absorbed per permutation.
    std::size_t rate_;
    /// The bytes absorbed since the last permutation.
    std::size_t position_ = 0;
};

} // namespace openxisf::detail
