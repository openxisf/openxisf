// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "codec/compressed_block.h"

#include <openxisf/error.h>

#include "codec/codecs.h"
#include "codec/shuffle.h"
#include "core/checked_math.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>

namespace openxisf::detail {

namespace {

constexpr int highest_level = 100;

// The sum of the sizes, or nothing when it does not fit in 64 bits.
std::optional<std::uint64_t> total(const std::vector<subblock>& subblocks, std::uint64_t subblock::* size) noexcept
{
    std::uint64_t sum = 0;
    for (const subblock& part : subblocks) {
        if (part.*size > std::numeric_limits<std::uint64_t>::max() - sum) {
            return std::nullopt;
        }
        sum += part.*size;
    }
    return sum;
}

std::string describe_total(const std::optional<std::uint64_t>& sum)
{
    return sum ? "add up to " + std::to_string(*sum) : "add up to more than 2^64";
}

void check_totals(const block_compression& compression, std::uint64_t stored_size)
{
    const std::optional<std::uint64_t> uncompressed = total(compression.subblocks, &subblock::uncompressed_size);
    if (uncompressed != compression.uncompressed_size) {
        throw invalid_data_error(errc::invalid_subblocks, "the uncompressed sizes of the subblocks " +
                                                              describe_total(uncompressed) +
                                                              ", not to the uncompressed size of the block, " +
                                                              std::to_string(compression.uncompressed_size));
    }
    const std::optional<std::uint64_t> compressed = total(compression.subblocks, &subblock::compressed_size);
    if (compressed != stored_size) {
        throw invalid_data_error(errc::invalid_subblocks,
                                 "the compressed sizes of the subblocks " + describe_total(compressed) +
                                     ", not to the size of the stored block, " + std::to_string(stored_size));
    }
}

// The largest subblock, compressed or not, that the decoder of codec takes.
std::uint64_t max_decoded_size(compression_codec codec) noexcept
{
    switch (codec) {
    case compression_codec::lz4:
    case compression_codec::lz4hc:
        return lz4_max_decoded_size;
    case compression_codec::zlib:
    case compression_codec::zstd:
        break;
    }
    return std::numeric_limits<std::uint64_t>::max();
}

// The most that the encoder of codec takes at once.
std::uint64_t max_encoded_size(compression_codec codec) noexcept
{
    switch (codec) {
    case compression_codec::zlib:
        return (std::uint64_t{1} << 32) - (std::uint64_t{1} << 24);
    case compression_codec::lz4:
    case compression_codec::lz4hc:
        return lz4_max_input_size;
    case compression_codec::zstd:
        break;
    }
    return std::numeric_limits<std::uint64_t>::max();
}

// A subblock that did not get smaller is stored as it is, which its two equal sizes tell. The specification does not
// say so, but PixInsight writes such subblocks.
bool stored_as_is(const subblock& part) noexcept
{
    return part.compressed_size == part.uncompressed_size;
}

// Spec §11.4.2: the abstract range 1 to 100 maps linearly to lowest to highest, rounded to the nearest level.
int scale(int level, int lowest, int highest) noexcept
{
    return lowest + (((level - 1) * (highest - lowest)) + ((highest_level - 1) / 2)) / (highest_level - 1);
}

void decompress_subblock(compression_codec codec, std::span<const std::byte> input, std::span<std::byte> output,
                         const limits& limits)
{
    switch (codec) {
    case compression_codec::zlib:
        zlib_decompress(input, output);
        return;
    case compression_codec::lz4:
    case compression_codec::lz4hc:
        lz4_decompress(input, output);
        return;
    case compression_codec::zstd:
        zstd_decompress(input, output, limits.max_zstd_window);
        return;
    }
}

void compress_subblock(compression_codec codec, std::span<const std::byte> input, int level,
                       std::vector<std::byte>& output)
{
    switch (codec) {
    case compression_codec::zlib:
        zlib_compress(input, level, output);
        return;
    case compression_codec::lz4:
        lz4_compress(input, output);
        return;
    case compression_codec::lz4hc:
        lz4hc_compress(input, level, output);
        return;
    case compression_codec::zstd:
        zstd_compress(input, level, output);
        return;
    }
}

} // namespace

std::uint64_t subblock_size(const compression_options& options) noexcept
{
    const std::uint64_t limit = max_encoded_size(options.codec);
    return options.max_subblock_size == 0 ? limit : std::min(options.max_subblock_size, limit);
}

int codec_level(compression_codec codec, int level)
{
    if (level < 0 || level > highest_level) {
        throw usage_error(errc::invalid_argument,
                          "the compression level " + std::to_string(level) + " is not between 0 and 100");
    }
    switch (codec) {
    case compression_codec::zlib:
        return level == 0 ? zlib_default_level : scale(level, 0, 9);
    case compression_codec::lz4hc:
        return level == 0 ? lz4hc_default_level : scale(level, 1, 12);
    case compression_codec::zstd:
        return level == 0 ? zstd_default_level : scale(level, 1, 22);
    case compression_codec::lz4:
        break;
    }
    return 0;
}

std::vector<subblock> subblocks_of(const block_compression& compression, std::uint64_t stored_size)
{
    if (!compression.subblocks.empty()) {
        check_totals(compression, stored_size);
    }
    std::vector<subblock> subblocks = compression.subblocks;
    if (subblocks.empty()) {
        subblocks.push_back({.compressed_size = stored_size, .uncompressed_size = compression.uncompressed_size});
    }
    const std::uint64_t limit = max_decoded_size(compression.codec);
    for (std::size_t i = 0; i < subblocks.size(); ++i) {
        if (!stored_as_is(subblocks[i]) &&
            (subblocks[i].compressed_size > limit || subblocks[i].uncompressed_size > limit)) {
            throw invalid_data_error(
                errc::invalid_subblocks,
                (compression.subblocks.empty() ? std::string("the block") : "subblock " + std::to_string(i + 1)) +
                    " is larger than " + std::string(codec_name(compression.codec)) + " can decode at once, " +
                    std::to_string(limit) + " bytes, and must be divided into subblocks");
        }
    }
    return subblocks;
}

std::vector<std::byte> decompress_block(std::span<const std::byte> stored, const block_compression& compression,
                                        const limits& limits, const subblock_progress& progress)
{
    const std::vector<subblock> subblocks = subblocks_of(compression, stored.size());
    if (limits.max_allocation != 0 && compression.uncompressed_size > limits.max_allocation) {
        throw limit_error(errc::allocation_too_large,
                          "the data block decompresses to " + std::to_string(compression.uncompressed_size) +
                              " bytes, more than the allocation limit of " + std::to_string(limits.max_allocation));
    }

    std::vector<std::byte> data(checked_cast<std::size_t>(compression.uncompressed_size));
    std::size_t in = 0;
    std::size_t out = 0;
    for (std::size_t i = 0; i < subblocks.size(); ++i) {
        const auto compressed = static_cast<std::size_t>(subblocks[i].compressed_size);
        const auto uncompressed = static_cast<std::size_t>(subblocks[i].uncompressed_size);
        try {
            if (stored_as_is(subblocks[i])) {
                std::ranges::copy(stored.subspan(in, compressed), data.begin() + static_cast<std::ptrdiff_t>(out));
            } else {
                decompress_subblock(compression.codec, stored.subspan(in, compressed),
                                    std::span(data).subspan(out, uncompressed), limits);
            }
        } catch (const integrity_error& failure) {
            if (subblocks.size() == 1) {
                throw;
            }
            throw integrity_error(failure.code(), "subblock " + std::to_string(i + 1) + " of " +
                                                      std::to_string(subblocks.size()) + ": " + failure.what());
        }
        in += compressed;
        out += uncompressed;
        if (progress) {
            progress(out);
        }
    }

    if (compression.item_size <= 1) {
        return data;
    }
    std::vector<std::byte> unshuffled(data.size());
    unshuffle_bytes(data, unshuffled, static_cast<std::size_t>(compression.item_size));
    return unshuffled;
}

compressed_block compress_block(std::span<const std::byte> data, const compression_options& options)
{
    const int level = codec_level(options.codec, options.level);
    const std::uint64_t limit = subblock_size(options);

    std::vector<std::byte> shuffled;
    if (options.item_size > 1) {
        shuffled.resize(data.size());
        shuffle_bytes(data, shuffled, static_cast<std::size_t>(options.item_size));
        data = shuffled;
    }

    compressed_block result{
        .compression = {.codec = options.codec, .uncompressed_size = data.size(), .item_size = options.item_size}};
    std::size_t offset = 0;
    do {
        const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(limit, data.size() - offset));
        const std::size_t start = result.data.size();
        const std::span<const std::byte> part = data.subspan(offset, size);
        compress_subblock(options.codec, part, level, result.data);
        if (result.data.size() - start >= size) {
            result.data.resize(start);
            result.data.insert(result.data.end(), part.begin(), part.end());
        }
        result.compression.subblocks.push_back(
            {.compressed_size = result.data.size() - start, .uncompressed_size = size});
        offset += size;
    } while (offset < data.size());
    if (result.compression.subblocks.size() == 1) {
        result.compression.subblocks.clear();
    }
    return result;
}

} // namespace openxisf::detail
