// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The codecs and byte shuffling (spec §10.6). The first byte of the input selects the codec (bits 0 and 1), the mode
// (bit 2) and the compression level (bits 3 and 4); the second is the item size of byte shuffling, and the next three
// a size, little-endian.
//
// - Decoding: the size is the uncompressed size of the block. A count of subblocks follows, and for each its compressed
//   and uncompressed sizes in two bytes each; the rest is the stored block. The block decodes to exactly its size, or
//   fails with the error of a corrupt block, a wrong subblock list or a limit.
// - Encoding: the size is the largest subblock (0 for no limit), and the rest is the data. The data compress into
//   subblocks within that size, which decode to the data again. The size grows when it would make more than 64
//   subblocks, which would only make the run slow.
//
// In both modes, unshuffling reverses shuffling, the SIMD kernels give the bytes of the scalar code, a part of the
// shuffled data is that part of the whole, and a block decompressed into memory of the caller is the block returned.
// Any other exception escapes and fails the run.

#include <openxisf/error.h>
#include <openxisf/limits.h>

#include "codec/compressed_block.h"
#include "codec/compression.h"
#include "codec/shuffle.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

namespace {

namespace detail = openxisf::detail;

void require(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

constexpr std::array<detail::compression_codec, 4> codecs{
    detail::compression_codec::zlib, detail::compression_codec::lz4, detail::compression_codec::lz4hc,
    detail::compression_codec::zstd};
constexpr std::array<int, 4> levels{0, 1, 50, 100};

// Small limits, so that the fuzzer reaches them.
constexpr openxisf::limits limits{.max_allocation = std::uint64_t{1} << 20, .max_zstd_window = std::uint64_t{1} << 16};

// Reads the input from the front.
class reader
{
public:
    explicit reader(std::span<const std::byte> input) noexcept : input_(input) {}

    std::uint64_t number(std::size_t size) noexcept
    {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < size && !input_.empty(); ++i) {
            value |= std::to_integer<std::uint64_t>(input_.front()) << (8 * i);
            input_ = input_.subspan(1);
        }
        return value;
    }

    [[nodiscard]] std::span<const std::byte> rest() const noexcept
    {
        return input_;
    }

private:
    std::span<const std::byte> input_;
};

void check_shuffling(std::span<const std::byte> data, std::size_t item_size)
{
    std::vector<std::byte> shuffled(data.size());
    detail::shuffle_bytes(data, shuffled, item_size, detail::shuffle_kernel::scalar);
    const std::size_t tail = item_size == 0 ? data.size() : data.size() % item_size;
    require(std::ranges::equal(std::span(shuffled).last(tail), data.last(tail)));
    std::vector<std::byte> simd(data.size());
    detail::shuffle_bytes(data, simd, item_size, detail::shuffle_kernel::simd);
    require(simd == shuffled);

    std::vector<std::byte> unshuffled(data.size());
    detail::unshuffle_bytes(shuffled, unshuffled, item_size, detail::shuffle_kernel::scalar);
    require(std::ranges::equal(unshuffled, data));
    detail::unshuffle_bytes(shuffled, simd, item_size, detail::shuffle_kernel::simd);
    require(std::ranges::equal(simd, data));

    // The middle third, which starts and ends inside runs.
    std::vector<std::byte> part(data.size() / 3);
    detail::shuffle_part(data, item_size, part.size(), part);
    require(std::ranges::equal(part, std::span(shuffled).subspan(part.size(), part.size())));
}

void decode(reader& input, detail::compression_codec codec, std::uint64_t item_size)
{
    detail::block_compression compression{.codec = codec, .uncompressed_size = input.number(3), .item_size = item_size};
    const std::uint64_t count = input.number(1) % 16;
    for (std::uint64_t i = 0; i < count; ++i) {
        compression.subblocks.push_back({.compressed_size = input.number(2), .uncompressed_size = input.number(2)});
    }
    const std::span<const std::byte> stored = input.rest();

    std::vector<std::byte> data;
    try {
        data = detail::decompress_block(stored, compression, limits);
    } catch (const openxisf::integrity_error& failure) {
        require(failure.code() == openxisf::errc::corrupt_compressed_data);
        return;
    } catch (const openxisf::invalid_data_error& failure) {
        require(failure.code() == openxisf::errc::invalid_subblocks);
        return;
    } catch (const openxisf::limit_error& failure) {
        require(failure.code() == openxisf::errc::allocation_too_large ||
                failure.code() == openxisf::errc::zstd_window_too_large);
        return;
    }
    require(data.size() == compression.uncompressed_size);
    std::vector<std::byte> destination(data.size());
    detail::decompress_block_into(stored, compression, limits, destination);
    require(destination == data);
    check_shuffling(data, item_size);
}

void encode(reader& input, detail::compression_codec codec, std::uint64_t item_size, int level)
{
    const std::uint64_t requested = input.number(3);
    const std::span<const std::byte> data = input.rest();
    const std::uint64_t max_subblock_size = requested == 0 ? 0 : std::max<std::uint64_t>(requested, data.size() / 64);
    const detail::compressed_block block = detail::compress_block(
        data, {.codec = codec, .item_size = item_size, .level = level, .max_subblock_size = max_subblock_size});

    require(block.compression.uncompressed_size == data.size());
    require(block.compression.subblocks.size() != 1);
    for (const detail::subblock& part : block.compression.subblocks) {
        require(max_subblock_size == 0 || part.uncompressed_size <= max_subblock_size);
    }
    require(std::ranges::equal(detail::decompress_block(block.data, block.compression, {}), data));
    check_shuffling(data, item_size);
}

} // namespace

// The entry point that libFuzzer, and the replay driver, call for every input. Its name is fixed by libFuzzer.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    reader input(std::as_bytes(std::span(data, size)));
    const std::uint64_t selector = input.number(1);
    const std::uint64_t item_size = input.number(1);
    const detail::compression_codec codec = codecs[selector & 3U];
    if ((selector & 4U) == 0) {
        decode(input, codec, item_size);
    } else {
        encode(input, codec, item_size, levels[(selector >> 3U) & 3U]);
    }
    return 0;
}
