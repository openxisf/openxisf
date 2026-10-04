// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/types.h>

#include "container/block_attributes.h"

#include <cstddef>
#include <cstdint>
#include <span>

// The layout of pixel data in memory: the byte order of their samples (spec §10.4) and their storage model (spec
// §8.5.3).

namespace openxisf::detail {

/// The size of the items whose byte order a data block gives, for samples of a format: the size of a sample, or of
/// each part of a complex sample, whose parts are stored one after the other.
[[nodiscard]] std::size_t byte_order_item_size(sample_format format) noexcept;

/// Puts the samples of data, of the given format and stored in the given byte order, in the byte order of the host.
void to_native_byte_order(std::span<std::byte> data, sample_format format, byte_order order) noexcept;

/// Copies the samples of source, stored in the model from, into destination in the other model. Both hold pixel_count ×
/// channels samples of sample_size bytes, and they do not overlap.
void convert_storage(std::span<const std::byte> source, std::span<std::byte> destination, pixel_storage from,
                     std::size_t pixel_count, std::size_t channels, std::size_t sample_size) noexcept;

} // namespace openxisf::detail
