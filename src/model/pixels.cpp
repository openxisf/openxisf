// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "model/pixels.h"

#include <openxisf/error.h>

#include "model/pixel_layout.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace openxisf::detail {

namespace {

// The data of the block of the image at index, in native byte order. options.progress follows the read and can cancel
// it; it is first called before anything is read.
std::vector<std::byte> read_native(const unit& opened, std::size_t index, const image_info& info,
                                   const pixel_read_options& options, std::size_t piece_size)
{
    const data_block& block = opened.blocks[opened.images.blocks[index]];
    if (!block.descriptor) {
        // Throws the error of the unavailable block, before anything is read.
        return read_block(opened.source, block, opened.limits);
    }
    const block_descriptor& descriptor = *block.descriptor;
    block_progress progress;
    if (options.progress) {
        progress = [&options, &block, total = work_size(descriptor)](std::uint64_t done) {
            if (!options.progress(done, total)) {
                throw cancelled_error(errc::cancelled, "the read of the pixel data was cancelled",
                                      {.element = block.path});
            }
        };
        progress(0);
    }
    std::vector<std::byte> data = read_block(opened.source, block, opened.limits, progress, piece_size);
    to_native_byte_order(data, info.sample_format, descriptor.order);
    return data;
}

// True when the samples must change their storage model to the one asked for. A single channel is stored the same way
// in both models.
bool converts_storage(const image_info& info, const pixel_read_options& options) noexcept
{
    return options.storage.value_or(info.pixel_storage) != info.pixel_storage && info.geometry.channels > 1;
}

void convert(const image_info& info, std::span<const std::byte> data, std::span<std::byte> destination)
{
    convert_storage(data, destination, info.pixel_storage, info.geometry.pixel_count(), info.geometry.channels,
                    sample_size(info.sample_format));
}

} // namespace

const image_info& image_at(const unit& opened, std::size_t index)
{
    if (index >= opened.images.infos.size()) {
        throw usage_error(errc::invalid_argument, "there is no image " + std::to_string(index) + " in a unit of " +
                                                      std::to_string(opened.images.infos.size()) + " images");
    }
    return opened.images.infos[index];
}

std::vector<std::byte> read_pixels(const unit& opened, std::size_t index, const pixel_read_options& options,
                                   std::size_t piece_size)
{
    const image_info& info = image_at(opened, index);
    std::vector<std::byte> data = read_native(opened, index, info, options, piece_size);
    if (!converts_storage(info, options)) {
        return data;
    }
    std::vector<std::byte> converted(data.size());
    convert(info, data, converted);
    return converted;
}

void read_pixels(const unit& opened, std::size_t index, std::span<std::byte> destination,
                 const pixel_read_options& options, std::size_t piece_size)
{
    const image_info& info = image_at(opened, index);
    // The size was checked against the data block when the unit was opened, so it fits in 64 bits.
    const std::uint64_t size = info.data_size();
    if (destination.size() != size) {
        throw usage_error(errc::invalid_argument, "the destination has " + std::to_string(destination.size()) +
                                                      " bytes, and the pixel data of image " + std::to_string(index) +
                                                      " have " + std::to_string(size));
    }
    const std::vector<std::byte> data = read_native(opened, index, info, options, piece_size);
    if (converts_storage(info, options)) {
        convert(info, data, destination);
    } else {
        std::ranges::copy(data, destination.begin());
    }
}

std::size_t typed_sample_count(const unit& opened, std::size_t index, sample_format format)
{
    const image_info& info = image_at(opened, index);
    if (info.sample_format != format) {
        throw usage_error(errc::invalid_argument, "the samples of image " + std::to_string(index) + " are " +
                                                      std::string(sample_format_name(info.sample_format)) +
                                                      " values, not " + std::string(sample_format_name(format)));
    }
    const std::uint64_t size = info.data_size();
    if (opened.limits.max_allocation != 0 && size > opened.limits.max_allocation) {
        throw limit_error(errc::allocation_too_large, "the pixel data of image " + std::to_string(index) + " have " +
                                                          std::to_string(size) +
                                                          " bytes, more than the allocation limit of " +
                                                          std::to_string(opened.limits.max_allocation));
    }
    return info.geometry.sample_count();
}

} // namespace openxisf::detail
