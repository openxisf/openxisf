// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "codec/compressed_block.h"

#include <openxisf/error.h>

#include "codec/codecs.h"
#include "codec/shuffle.h"
#include "core/allocation.h"
#include "core/checked_math.h"
#include "core/parallel.h"
#include "core/scratch_buffer.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>

namespace openxisf::detail {

namespace {

constexpr int highest_level = 100;

// The subblocks that compress_subblocks() compresses at once take at most about this many bytes of data, unless one
// alone is larger; their shuffled copies and the output of the codec take about as much again.
constexpr std::uint64_t parallel_compression_budget = std::uint64_t{1} << 30;

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
// say so, but PixInsight writes such subblocks, and reads every subblock that is not smaller than its data so.
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

int default_abstract_level(compression_codec codec) noexcept
{
    // The inverse of scale(), rounded to the nearest abstract level.
    const auto inverse = [](int level, int lowest, int highest) {
        return 1 + (((level - lowest) * (highest_level - 1)) + ((highest - lowest) / 2)) / (highest - lowest);
    };
    switch (codec) {
    case compression_codec::zlib:
        return inverse(zlib_default_level, 0, 9);
    case compression_codec::lz4hc:
        return inverse(lz4hc_default_level, 1, 12);
    case compression_codec::zstd:
        return inverse(zstd_default_level, 1, 22);
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

namespace {

// The decompressed block is allocated by the library: its uncompressed size must be within the limit.
void check_decompressed_allocation(const block_compression& compression, const limits& limits)
{
    check_allocation(compression.uncompressed_size, limits, "the data block decompresses to");
}

// Decompresses the subblocks of a block, which subblocks_of() checked, each into its place in data, which has the
// uncompressed size of the block. The subblocks are independent, so each batch of as many as the threads that can run
// at once is decompressed in parallel; progress follows on the calling thread, subblock by subblock, after each batch.
// A Zstandard frame that does not declare its size is a batch of its own: it needs a window buffer of up to
// limits.max_zstd_window, which then bounds the memory of the whole read, not that of each thread.
void decompress_subblocks(std::span<const std::byte> stored, compression_codec codec,
                          const std::vector<subblock>& subblocks, const limits& limits, std::span<std::byte> data,
                          const subblock_progress& progress)
{
    // Where each subblock starts in stored and in data, and where the last one ends.
    std::vector<std::size_t> in(subblocks.size() + 1);
    std::vector<std::size_t> out(subblocks.size() + 1);
    for (std::size_t i = 0; i < subblocks.size(); ++i) {
        in[i + 1] = in[i] + static_cast<std::size_t>(subblocks[i].compressed_size);
        out[i + 1] = out[i] + static_cast<std::size_t>(subblocks[i].uncompressed_size);
    }
    const auto decompress = [&](std::size_t i) {
        const std::span<const std::byte> input = stored.subspan(in[i], in[i + 1] - in[i]);
        const std::span<std::byte> output = data.subspan(out[i], out[i + 1] - out[i]);
        try {
            if (stored_as_is(subblocks[i])) {
                std::ranges::copy(input, output.begin());
            } else {
                decompress_subblock(codec, input, output, limits);
            }
        } catch (const integrity_error& failure) {
            if (subblocks.size() == 1) {
                throw;
            }
            throw integrity_error(failure.code(), "subblock " + std::to_string(i + 1) + " of " +
                                                      std::to_string(subblocks.size()) + ": " + failure.what());
        }
    };
    const auto needs_window = [&](std::size_t i) {
        return codec == compression_codec::zstd && !stored_as_is(subblocks[i]) &&
               zstd_needs_window(stored.subspan(in[i], in[i + 1] - in[i]));
    };
    const std::size_t batch = concurrency();
    for (std::size_t first = 0; first < subblocks.size();) {
        std::size_t count = 1;
        if (!needs_window(first)) {
            while (count < batch && first + count < subblocks.size() && !needs_window(first + count)) {
                ++count;
            }
        }
        parallel_for(count, [&](std::size_t k) { decompress(first + k); });
        if (progress) {
            for (std::size_t i = first; i < first + count; ++i) {
                progress(out[i + 1]);
            }
        }
        first += count;
    }
}

// Decompresses a block into destination, which has its uncompressed size, through a buffer when it is shuffled.
void decompress_into(std::span<const std::byte> stored, const block_compression& compression,
                     const std::vector<subblock>& subblocks, const limits& limits, std::span<std::byte> destination,
                     const subblock_progress& progress)
{
    if (compression.item_size <= 1) {
        decompress_subblocks(stored, compression.codec, subblocks, limits, destination, progress);
        return;
    }
    check_decompressed_allocation(compression, limits);
    // The subblocks fill it or throw.
    scratch_buffer shuffled(destination.size());
    decompress_subblocks(stored, compression.codec, subblocks, limits, shuffled.bytes(), progress);
    unshuffle_bytes(shuffled.bytes(), destination, static_cast<std::size_t>(compression.item_size));
}

} // namespace

std::vector<std::byte> decompress_block(std::span<const std::byte> stored, const block_compression& compression,
                                        const limits& limits, const subblock_progress& progress)
{
    const std::vector<subblock> subblocks = subblocks_of(compression, stored.size());
    check_decompressed_allocation(compression, limits);
    std::vector<std::byte> data(checked_cast<std::size_t>(compression.uncompressed_size));
    decompress_into(stored, compression, subblocks, limits, data, progress);
    return data;
}

void decompress_block_into(std::span<const std::byte> stored, const block_compression& compression,
                           const limits& limits, std::span<std::byte> destination, const subblock_progress& progress)
{
    const std::vector<subblock> subblocks = subblocks_of(compression, stored.size());
    if (destination.size() != compression.uncompressed_size) {
        throw usage_error(errc::invalid_argument, "the destination has " + std::to_string(destination.size()) +
                                                      " bytes, and the data block decompresses to " +
                                                      std::to_string(compression.uncompressed_size));
    }
    decompress_into(stored, compression, subblocks, limits, destination, progress);
}

block_compression compress_subblocks(std::span<const std::byte> data, const compression_options& options,
                                     const subblock_store& store)
{
    const int level = codec_level(options.codec, options.level);
    const std::uint64_t limit = subblock_size(options);
    const bool shuffled = options.item_size > 1;
    // An empty block is one empty subblock.
    const std::uint64_t count = data.empty() ? 1 : (data.size() / limit) + (data.size() % limit == 0 ? 0 : 1);

    // A subblock in the making: its data, shuffled when they are, and the output of the codec.
    struct piece
    {
        std::span<const std::byte> input{};
        std::vector<std::byte> shuffled_input{};
        std::vector<std::byte> output{};
    };
    const auto compress = [&](std::uint64_t index, piece& next) {
        const std::size_t offset = index * limit;
        const std::size_t size = std::min<std::uint64_t>(limit, data.size() - offset);
        next.input = data.subspan(offset, size);
        if (shuffled) {
            next.shuffled_input.resize(size);
            shuffle_part(data, static_cast<std::size_t>(options.item_size), offset, next.shuffled_input);
            next.input = next.shuffled_input;
        }
        next.output.clear();
        compress_subblock(options.codec, next.input, level, next.output);
    };

    // The subblocks are independent, so a batch of them is compressed in parallel, as many as the threads that can run
    // at once within a budget of memory, and they are stored in order on the calling thread.
    const std::uint64_t batch =
        std::min<std::uint64_t>(concurrency(), std::max<std::uint64_t>(1, parallel_compression_budget / limit));
    const std::size_t in_flight = std::min(batch, count);
    std::vector<piece> pieces(in_flight);
    block_compression result{.codec = options.codec, .uncompressed_size = data.size(), .item_size = options.item_size};
    for (std::uint64_t first = 0; first < count; first += batch) {
        const std::size_t size = std::min(batch, count - first);
        parallel_for(size, [&](std::size_t k) { compress(first + k, pieces[k]); });
        for (std::size_t k = 0; k < size; ++k) {
            const piece& done = pieces[k];
            const std::span<const std::byte> stored =
                done.output.size() >= done.input.size() ? done.input : std::span<const std::byte>(done.output);
            const subblock sizes{.compressed_size = stored.size(), .uncompressed_size = done.input.size()};
            store(stored, sizes);
            result.subblocks.push_back(sizes);
        }
    }
    return result;
}

compressed_block compress_block(std::span<const std::byte> data, const compression_options& options)
{
    compressed_block result;
    result.compression =
        compress_subblocks(data, options, [&result](std::span<const std::byte> stored, const subblock& /*sizes*/) {
            result.data.insert(result.data.end(), stored.begin(), stored.end());
        });
    if (result.compression.subblocks.size() == 1) {
        result.compression.subblocks.clear();
    }
    return result;
}

} // namespace openxisf::detail
