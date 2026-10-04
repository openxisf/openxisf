// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The codec adapters, one subblock at a time (spec §10.6.3 to §10.6.10): each decoder fills its output exactly from all
// of its input, or fails with corrupt_compressed_data. Round trips of every codec are in compressed_block.cpp; here the
// streams are malformed, or built by hand from RFC 1950, the LZ4 block format and RFC 8878.

#include "codec/codecs.h"

#include <openxisf/error.h>

#include "support/bytes.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <span>
#include <vector>

namespace {

using openxisf::errc;
using openxisf::detail::lz4_compress;
using openxisf::detail::lz4_decompress;
using openxisf::detail::zlib_compress;
using openxisf::detail::zlib_decompress;
using openxisf::detail::zstd_compress;
using openxisf::detail::zstd_decompress;
using openxisf::test::bytes;
using openxisf::test::pattern;
using openxisf::test::throws;

using decoder = std::function<void(std::span<const std::byte>, std::span<std::byte>)>;

std::vector<std::byte> byte_values(std::initializer_list<unsigned int> values)
{
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const unsigned int value : values) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

std::vector<std::byte> joined(std::vector<std::byte> first, const std::vector<std::byte>& second)
{
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

// Decodes input into a buffer of size bytes.
std::vector<std::byte> decoded(const decoder& decode, std::span<const std::byte> input, std::size_t size)
{
    std::vector<std::byte> output(size);
    decode(input, output);
    return output;
}

testing::AssertionResult corrupt(const decoder& decode, std::span<const std::byte> input, std::size_t size)
{
    return throws<openxisf::integrity_error>(errc::corrupt_compressed_data,
                                             [&] { (void)decoded(decode, input, size); });
}

// The failures that every decoder reports, for a valid stream of data: too little or too much output, missing or extra
// input, and no input.
void expect_exact_sizes(const decoder& decode, const std::vector<std::byte>& stream, const std::vector<std::byte>& data)
{
    EXPECT_EQ(decoded(decode, stream, data.size()), data);
    EXPECT_TRUE(corrupt(decode, stream, data.size() - 1));
    EXPECT_TRUE(corrupt(decode, stream, data.size() + 1));
    EXPECT_TRUE(corrupt(decode, std::span(stream).first(stream.size() - 1), data.size()));
    EXPECT_TRUE(corrupt(decode, std::span(stream).first(stream.size() / 2), data.size()));
    EXPECT_TRUE(corrupt(decode, joined(stream, bytes("x")), data.size()));
    EXPECT_TRUE(corrupt(decode, {}, data.size()));
}

// ---------------------------------------------------------------------------------------------------------------------
// zlib (RFC 1950)

void zlib_decoder(std::span<const std::byte> input, std::span<std::byte> output)
{
    zlib_decompress(input, output);
}

std::vector<std::byte> zlib_stream(std::span<const std::byte> data, std::size_t max_piece = 0xFFFF'FFFF)
{
    std::vector<std::byte> stream;
    zlib_compress(data, 6, stream, max_piece);
    return stream;
}

TEST(zlib_codec, decodes_exactly_the_declared_size_from_the_whole_stream)
{
    const std::vector<std::byte> data = pattern(1000);
    expect_exact_sizes(zlib_decoder, zlib_stream(data), data);
    EXPECT_TRUE(decoded(zlib_decoder, zlib_stream({}), 0).empty());
}

TEST(zlib_codec, the_adler32_check_value_covers_the_data)
{
    std::vector<std::byte> stream = zlib_stream(pattern(1000));
    stream.back() ^= std::byte{0x01};
    EXPECT_TRUE(corrupt(zlib_decoder, stream, 1000));
}

TEST(zlib_codec, refuses_streams_without_the_zlib_wrapper_and_preset_dictionaries)
{
    // Raw deflate data: the stream without its 2-byte header and 4-byte check value.
    const std::vector<std::byte> stream = zlib_stream(pattern(1000));
    EXPECT_TRUE(corrupt(zlib_decoder, std::span(stream).subspan(2, stream.size() - 6), 1000));
    // A header with the FDICT flag, followed by the identifier of a dictionary.
    EXPECT_TRUE(corrupt(zlib_decoder, joined(byte_values({0x78, 0x20, 0, 0, 0, 1}), stream), 1000));
}

TEST(zlib_codec, works_in_pieces_both_ways)
{
    // The pieces stand for the 32-bit counters of zlib, so that blocks above 4 GiB need no 4 GiB buffers.
    const std::vector<std::byte> data = pattern(10'007);
    const std::vector<std::byte> stream = zlib_stream(data, 7);
    EXPECT_EQ(stream, zlib_stream(data));
    for (const std::size_t piece : {1U, 7U, 4096U}) {
        const decoder in_pieces = [piece](std::span<const std::byte> input, std::span<std::byte> output) {
            zlib_decompress(input, output, piece);
        };
        expect_exact_sizes(in_pieces, stream, data);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// LZ4 (the block format)

void lz4_decoder(std::span<const std::byte> input, std::span<std::byte> output)
{
    lz4_decompress(input, output);
}

TEST(lz4_codec, decodes_exactly_the_declared_size_from_the_whole_block)
{
    const std::vector<std::byte> data = pattern(1000);
    std::vector<std::byte> block;
    lz4_compress(data, block);
    expect_exact_sizes(lz4_decoder, block, data);
}

TEST(lz4_codec, decodes_blocks_built_by_hand)
{
    // A sequence of 3 literals and nothing else; the empty block is a token without literals.
    EXPECT_EQ(decoded(lz4_decoder, byte_values({0x30, 'a', 'b', 'c'}), 3), bytes("abc"));
    EXPECT_TRUE(decoded(lz4_decoder, byte_values({0x00}), 0).empty());
    // A match whose offset reaches before the start of the output.
    EXPECT_TRUE(corrupt(lz4_decoder, byte_values({0x10, 'a', 0x05, 0x00, 0x50, 'a', 'b', 'c', 'd', 'e'}), 10));
}

// ---------------------------------------------------------------------------------------------------------------------
// Zstandard (RFC 8878)

constexpr std::uint64_t no_window_limit = 0;

decoder zstd_decoder(std::uint64_t max_window)
{
    return [max_window](std::span<const std::byte> input, std::span<std::byte> output) {
        zstd_decompress(input, output, max_window);
    };
}

std::vector<std::byte> magic_number()
{
    return byte_values({0x28, 0xB5, 0x2F, 0xFD});
}

// The header of the last block of a frame, a Raw_Block of data (RFC 8878 §3.1.1.2).
std::vector<std::byte> raw_block(const std::vector<std::byte>& data)
{
    const auto header = static_cast<std::uint32_t>(1U | (data.size() << 3U));
    return joined(byte_values({header & 0xFFU, (header >> 8U) & 0xFFU, (header >> 16U) & 0xFFU}), data);
}

// A frame that does not declare its content size, with a window of 2^window_log bytes.
std::vector<std::byte> unsized_frame(const std::vector<std::byte>& data, unsigned int window_log)
{
    return joined(joined(magic_number(), byte_values({0x00, (window_log - 10U) << 3U})), raw_block(data));
}

// A single-segment frame that declares a content size below 256, which has no window descriptor.
std::vector<std::byte> sized_frame(const std::vector<std::byte>& data, unsigned int declared)
{
    return joined(joined(magic_number(), byte_values({0x20, declared})), raw_block(data));
}

TEST(zstd_codec, decodes_exactly_the_declared_size_from_the_whole_frame)
{
    const std::vector<std::byte> data = pattern(1000);
    std::vector<std::byte> frame;
    zstd_compress(data, 3, frame);
    expect_exact_sizes(zstd_decoder(no_window_limit), frame, data);
    expect_exact_sizes(zstd_decoder(no_window_limit), unsized_frame(pattern(100), 10), pattern(100));
    EXPECT_TRUE(decoded(zstd_decoder(no_window_limit), unsized_frame({}, 10), 0).empty());
}

TEST(zstd_codec, the_declared_content_size_must_be_the_uncompressed_size)
{
    const std::vector<std::byte> data = pattern(100);
    EXPECT_EQ(decoded(zstd_decoder(no_window_limit), sized_frame(data, 100), 100), data);
    // The raw block holds 100 bytes either way; the declaration is wrong.
    EXPECT_TRUE(corrupt(zstd_decoder(no_window_limit), sized_frame(data, 101), 101));
    EXPECT_TRUE(corrupt(zstd_decoder(no_window_limit), sized_frame(data, 99), 99));
}

TEST(zstd_codec, a_frame_that_declares_another_size_is_corrupt_whatever_its_window)
{
    // A single-segment frame declaring 1 GiB in 4 bytes, whose window is therefore 1 GiB too, for an output of 100
    // bytes: the size is wrong before the window matters.
    const std::vector<std::byte> data = pattern(100);
    const std::vector<std::byte> frame =
        joined(joined(magic_number(), byte_values({0xA0, 0x00, 0x00, 0x00, 0x40})), raw_block(data));
    EXPECT_TRUE(corrupt(zstd_decoder(std::uint64_t{1} << 20), frame, 100));
}

TEST(zstd_codec, a_subblock_is_exactly_one_frame_of_compressed_data)
{
    const std::vector<std::byte> data = pattern(100);
    const std::vector<std::byte> frame = unsized_frame(data, 10);
    EXPECT_TRUE(corrupt(zstd_decoder(no_window_limit), joined(frame, frame), 200));
    // A skippable frame (RFC 8878 §3.1.2) holds no data, before a frame or alone.
    const std::vector<std::byte> skippable = byte_values({0x50, 0x2A, 0x4D, 0x18, 1, 0, 0, 0, 0});
    EXPECT_TRUE(corrupt(zstd_decoder(no_window_limit), joined(skippable, frame), 100));
    EXPECT_TRUE(corrupt(zstd_decoder(no_window_limit), skippable, 0));
    EXPECT_TRUE(corrupt(zstd_decoder(no_window_limit), bytes("not a frame"), 100));
}

TEST(zstd_codec, a_frame_whose_window_exceeds_the_limit_is_refused)
{
    // The limit is rounded down to a power of two.
    const std::vector<std::byte> data = pattern(100);
    const std::vector<std::byte> frame = unsized_frame(data, 20);
    EXPECT_EQ(decoded(zstd_decoder(std::uint64_t{1} << 20), frame, 100), data);
    EXPECT_EQ(decoded(zstd_decoder(no_window_limit), frame, 100), data);
    EXPECT_TRUE(throws<openxisf::limit_error>(
        errc::zstd_window_too_large, [&] { (void)decoded(zstd_decoder((std::uint64_t{1} << 20) - 1), frame, 100); }));
    // Below the smallest window of 1 KiB, the limit is 1 KiB.
    EXPECT_EQ(decoded(zstd_decoder(1), unsized_frame(data, 10), 100), data);
}

TEST(zstd_codec, a_frame_that_declares_its_size_needs_no_window)
{
    // A frame that is not single-segment, with a window of 1 GiB and a content size of 100 in 4 bytes, decodes into the
    // output directly.
    const std::vector<std::byte> data = pattern(100);
    const std::vector<std::byte> frame =
        joined(joined(magic_number(), byte_values({0x80, (30U - 10U) << 3U, 100, 0, 0, 0})), raw_block(data));
    EXPECT_EQ(decoded(zstd_decoder(1024), frame, 100), data);
}

} // namespace
