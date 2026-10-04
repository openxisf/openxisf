// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "crypto/message_blocks.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace openxisf::detail {

/// SHA-256 (FIPS 180-4, §6.2).
class sha256
{
public:
    static constexpr std::size_t digest_size = 32;

    void update(std::span<const std::byte> data);

    /// Writes the digest of the message. The object cannot be used afterwards.
    void finish(std::span<std::byte, digest_size> digest);

private:
    void compress(std::span<const std::byte, 64> block) noexcept;

    std::array<std::uint32_t, 8> state_{0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
                                        0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U};
    message_blocks<64, 8> blocks_{};
};

/// SHA-512 (FIPS 180-4, §6.4).
class sha512
{
public:
    static constexpr std::size_t digest_size = 64;

    void update(std::span<const std::byte> data);

    /// Writes the digest of the message. The object cannot be used afterwards.
    void finish(std::span<std::byte, digest_size> digest);

private:
    void compress(std::span<const std::byte, 128> block) noexcept;

    std::array<std::uint64_t, 8> state_{0x6A09E667F3BCC908U, 0xBB67AE8584CAA73BU, 0x3C6EF372FE94F82BU,
                                        0xA54FF53A5F1D36F1U, 0x510E527FADE682D1U, 0x9B05688C2B3E6C1FU,
                                        0x1F83D9ABFB41BD6BU, 0x5BE0CD19137E2179U};
    message_blocks<128, 16> blocks_{};
};

} // namespace openxisf::detail
