// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <cstddef>
#include <span>

// Byte shuffling (spec §10.6.2). A block of n items of s bytes each is stored as s runs of n bytes: first byte 0 of
// every item, then byte 1 of every item, and so on. The bytes after the last complete item follow unchanged.

namespace openxisf::detail {

/// Writes the shuffled bytes of input to output, which has the same size and does not overlap it. An item size of 0 or
/// 1, or one larger than the input, copies the input unchanged.
void shuffle_bytes(std::span<const std::byte> input, std::span<std::byte> output, std::size_t item_size);

/// Reverses shuffle_bytes(): writes the unshuffled bytes of input to output, under the same conditions.
void unshuffle_bytes(std::span<const std::byte> input, std::span<std::byte> output, std::size_t item_size);

} // namespace openxisf::detail
