// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "crypto/sha1.h"

#include "core/endian.h"

#include <bit>

namespace openxisf::detail {

void sha1::update(std::span<const std::byte> data)
{
    blocks_.update(data, [this](std::span<const std::byte, 64> block) { compress(block); });
}

void sha1::finish(std::span<std::byte, digest_size> digest)
{
    blocks_.finish([this](std::span<const std::byte, 64> block) { compress(block); });
    for (std::size_t i = 0; i < state_.size(); ++i) {
        store_big_endian<std::uint32_t>(digest.subspan(4 * i).first<4>(), state_[i]);
    }
}

// FIPS 180-4, §6.1.2.
void sha1::compress(std::span<const std::byte, 64> block) noexcept
{
    std::array<std::uint32_t, 80> schedule{};
    for (std::size_t t = 0; t < 16; ++t) {
        schedule[t] = load_big_endian<std::uint32_t>(block.subspan(4 * t).first<4>());
    }
    for (std::size_t t = 16; t < 80; ++t) {
        schedule[t] = std::rotl(schedule[t - 3] ^ schedule[t - 8] ^ schedule[t - 14] ^ schedule[t - 16], 1);
    }

    auto [a, b, c, d, e] = state_;
    for (std::size_t t = 0; t < 80; ++t) {
        // The functions and constants of §4.1.1 and §4.2.1: Ch, Parity, Maj and Parity, 20 rounds each.
        std::uint32_t mixed = 0;
        std::uint32_t constant = 0;
        if (t < 20) {
            mixed = (b & c) | (~b & d);
            constant = 0x5A827999U;
        } else if (t < 40) {
            mixed = b ^ c ^ d;
            constant = 0x6ED9EBA1U;
        } else if (t < 60) {
            mixed = (b & c) | (b & d) | (c & d);
            constant = 0x8F1BBCDCU;
        } else {
            mixed = b ^ c ^ d;
            constant = 0xCA62C1D6U;
        }
        const std::uint32_t next = std::rotl(a, 5) + mixed + e + constant + schedule[t];
        e = d;
        d = c;
        c = std::rotl(b, 30);
        b = a;
        a = next;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
}

} // namespace openxisf::detail
