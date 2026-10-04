// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "model/pixel_layout.h"

#include "core/endian.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace openxisf::detail {

std::size_t byte_order_item_size(sample_format format) noexcept
{
    const std::size_t size = sample_size(format);
    return format == sample_format::complex32 || format == sample_format::complex64 ? size / 2 : size;
}

void to_native_byte_order(std::span<std::byte> data, sample_format format, byte_order order) noexcept
{
    const bool native = (order == byte_order::little) == (std::endian::native == std::endian::little);
    if (!native) {
        swap_byte_order(data, byte_order_item_size(format));
    }
}

// Sample p of channel c is at c × pixel_count + p in the planar model, and at p × channels + c in the normal one. The
// copy walks the destination in order.
void convert_storage(std::span<const std::byte> source, std::span<std::byte> destination, pixel_storage from,
                     std::size_t pixel_count, std::size_t channels, std::size_t sample_size) noexcept
{
    if (channels == 1) {
        // Both models store a single channel the same way.
        std::ranges::copy(source, destination.begin());
        return;
    }
    std::byte* out = destination.data();
    if (from == pixel_storage::planar) {
        for (std::size_t p = 0; p < pixel_count; ++p) {
            for (std::size_t c = 0; c < channels; ++c) {
                std::memcpy(out, source.data() + (((c * pixel_count) + p) * sample_size), sample_size);
                out += sample_size;
            }
        }
    } else {
        for (std::size_t c = 0; c < channels; ++c) {
            for (std::size_t p = 0; p < pixel_count; ++p) {
                std::memcpy(out, source.data() + (((p * channels) + c) * sample_size), sample_size);
                out += sample_size;
            }
        }
    }
}

} // namespace openxisf::detail
