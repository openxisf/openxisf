// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "core/endian.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace openxisf::detail {

/// The message processing that SHA-1 and SHA-2 share (FIPS 180-4, §5.1 and §5.2): the message is cut into blocks of
/// BlockSize bytes, and the last block is padded with a 1 bit, zeros, and the length of the message in bits as a
/// big-endian integer of LengthSize bytes. Each complete block goes to the compression function of the algorithm, a
/// callable that takes std::span<const std::byte, BlockSize>.
template <std::size_t BlockSize, std::size_t LengthSize> class message_blocks
{
public:
    template <typename Compress> void update(std::span<const std::byte> data, Compress compress)
    {
        length_ += data.size();
        if (buffered_ > 0) {
            const std::size_t taken = std::min(data.size(), BlockSize - buffered_);
            std::ranges::copy(data.first(taken), buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_));
            buffered_ += taken;
            data = data.subspan(taken);
            if (buffered_ < BlockSize) {
                return;
            }
            compress(std::span<const std::byte, BlockSize>(buffer_));
            buffered_ = 0;
        }
        for (; data.size() >= BlockSize; data = data.subspan(BlockSize)) {
            compress(data.template first<BlockSize>());
        }
        std::ranges::copy(data, buffer_.begin());
        buffered_ = data.size();
    }

    /// Pads the message and compresses its last blocks. Nothing can be added afterwards.
    template <typename Compress> void finish(Compress compress)
    {
        buffer_[buffered_] = std::byte{0x80};
        std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_) + 1, buffer_.end(), std::byte{0});
        if (buffered_ + 1 > BlockSize - LengthSize) {
            compress(std::span<const std::byte, BlockSize>(buffer_));
            std::ranges::fill(buffer_, std::byte{0});
        }
        // A length of 2^64 bytes needs 67 bits; a 128-bit field gets the 3 high bits too.
        const std::span<std::byte, BlockSize> block(buffer_);
        store_big_endian<std::uint64_t>(block.template last<8>(), length_ << 3U);
        if constexpr (LengthSize == 16) {
            block[BlockSize - 9] = static_cast<std::byte>(length_ >> 61U);
        }
        compress(std::span<const std::byte, BlockSize>(buffer_));
    }

private:
    static_assert(LengthSize == 8 || LengthSize == 16, "SHA-1 and SHA-2 have length fields of 64 or 128 bits");

    std::array<std::byte, BlockSize> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t length_ = 0;
};

} // namespace openxisf::detail
