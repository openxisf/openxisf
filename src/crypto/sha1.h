// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#pragma once

#include "crypto/message_blocks.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace openxisf::detail {

/// SHA-1 (FIPS 180-4, §6.1), the hashing algorithm that spec §10.5 recommends for block checksums.
class sha1
{
public:
    static constexpr std::size_t digest_size = 20;

    void update(std::span<const std::byte> data);

    /// Writes the digest of the message. The object cannot be used afterwards.
    void finish(std::span<std::byte, digest_size> digest);

private:
    void compress(std::span<const std::byte, 64> block) noexcept;

    std::array<std::uint32_t, 5> state_{0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U, 0xC3D2E1F0U};
    message_blocks<64, 8> blocks_{};
};

} // namespace openxisf::detail
