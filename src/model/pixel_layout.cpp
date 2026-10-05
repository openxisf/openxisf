// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "model/pixel_layout.h"

#include "core/endian.h"
#include "core/parallel.h"

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

namespace {

// A conversion between the storage models: pixel_count × channels samples of sample_size bytes, stored in the model
// from at source, copied in the other one to destination.
struct conversion
{
    const std::byte* source = nullptr;
    std::byte* destination = nullptr;
    pixel_storage from = pixel_storage::planar;
    std::size_t pixel_count = 0;
    std::size_t channels = 0;
    std::size_t sample_size = 0;
};

// Converts the samples of the pixels [first, end). Sample p of channel c is at c × pixel_count + p in the planar model,
// and at p × channels + c in the normal one; the copy walks the destination in order. SampleSize is the sample size
// when it is known at compile time, which makes each copy a single move, and 0 otherwise.
template <std::size_t SampleSize>
void convert_pixels(const conversion& job, std::size_t first, std::size_t end) noexcept
{
    const std::size_t size = SampleSize == 0 ? job.sample_size : SampleSize;
    if (job.from == pixel_storage::planar) {
        std::byte* out = job.destination + (first * job.channels * size);
        for (std::size_t p = first; p < end; ++p) {
            for (std::size_t c = 0; c < job.channels; ++c) {
                std::memcpy(out, job.source + (((c * job.pixel_count) + p) * size), size);
                out += size;
            }
        }
    } else {
        for (std::size_t c = 0; c < job.channels; ++c) {
            std::byte* out = job.destination + (((c * job.pixel_count) + first) * size);
            for (std::size_t p = first; p < end; ++p) {
                std::memcpy(out, job.source + (((p * job.channels) + c) * size), size);
                out += size;
            }
        }
    }
}

void convert_pixels(const conversion& job, std::size_t first, std::size_t end) noexcept
{
    // The sizes of the sample formats.
    switch (job.sample_size) {
    case 1:
        convert_pixels<1>(job, first, end);
        return;
    case 2:
        convert_pixels<2>(job, first, end);
        return;
    case 4:
        convert_pixels<4>(job, first, end);
        return;
    case 8:
        convert_pixels<8>(job, first, end);
        return;
    case 16:
        convert_pixels<16>(job, first, end);
        return;
    default:
        convert_pixels<0>(job, first, end);
        return;
    }
}

} // namespace

void convert_storage(std::span<const std::byte> source, std::span<std::byte> destination, pixel_storage from,
                     std::size_t pixel_count, std::size_t channels, std::size_t sample_size)
{
    if (channels == 1) {
        // Both models store a single channel the same way.
        std::ranges::copy(source, destination.begin());
        return;
    }
    const conversion job{.source = source.data(),
                         .destination = destination.data(),
                         .from = from,
                         .pixel_count = pixel_count,
                         .channels = channels,
                         .sample_size = sample_size};
    // The pixels are converted in pieces of about 1 MiB, in parallel with oneTBB.
    const std::size_t piece = std::max<std::size_t>(1, (std::size_t{1} << 20) / (channels * sample_size));
    parallel_for((pixel_count + piece - 1) / piece, [&job, piece, pixel_count](std::size_t k) {
        convert_pixels(job, k * piece, std::min(pixel_count, (k + 1) * piece));
    });
}

} // namespace openxisf::detail
