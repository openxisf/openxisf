// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Pixel reads from 16 threads at once on one reader, whose images are attached, compressed and embedded, on a source
// that supports concurrent reads and on one that the library serializes, and in a data blocks file. ThreadSanitizer
// runs these tests in CI.

#include <openxisf/io.h>
#include <openxisf/reader.h>
#include <openxisf/types.h>

#include "codec/compressed_block.h"
#include "codec/compression.h"
#include "core/xoshiro.h"
#include "support/bytes.h"
#include "support/fixture_builder.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using openxisf::pixel_storage;
using openxisf::reader;

constexpr int thread_count = 16;
constexpr int reads_per_thread = 40;
constexpr std::size_t width = 64;
constexpr std::size_t height = 48;
constexpr std::size_t channels = 3;

// The samples of an RGB UInt16 image in a storage model: 1000c + p for pixel p of channel c, wrapped to 16 bits.
std::vector<std::uint16_t> values(pixel_storage storage)
{
    constexpr std::size_t pixels = width * height;
    std::vector<std::uint16_t> result(pixels * channels);
    for (std::size_t c = 0; c < channels; ++c) {
        for (std::size_t p = 0; p < pixels; ++p) {
            result[storage == pixel_storage::planar ? (c * pixels) + p : (p * channels) + c] =
                static_cast<std::uint16_t>((1000 * c) + p);
        }
    }
    return result;
}

// The samples in native byte order, as read_pixels() returns them.
std::vector<std::byte> samples(pixel_storage storage)
{
    const std::vector<std::uint16_t> native = values(storage);
    std::vector<std::byte> bytes(native.size() * sizeof(std::uint16_t));
    std::memcpy(bytes.data(), native.data(), bytes.size());
    return bytes;
}

// The data blocks of the first two images: the planar samples in little-endian byte order, and the same compressed
// with zlib and byte shuffling.
std::vector<std::vector<std::byte>> image_blocks()
{
    std::vector<std::byte> planar;
    for (const std::uint16_t value : values(pixel_storage::planar)) {
        planar.push_back(static_cast<std::byte>(value & 0xFFU));
        planar.push_back(static_cast<std::byte>(value >> 8U));
    }
    openxisf::detail::compressed_block compressed =
        openxisf::detail::compress_block(planar, {.codec = openxisf::detail::compression_codec::zlib, .item_size = 2});
    return {std::move(planar), std::move(compressed.data)};
}

// The header of three images: the blocks of image_blocks() at the given locations, and an image of two pixels
// embedded.
std::string header_of_three_images(std::string_view first, std::string_view second)
{
    const std::string geometry = R"(geometry=")" + std::to_string(width) + ":" + std::to_string(height) + R"(:3")";
    return openxisf::test::header_xml(
        "<Image " + geometry + R"( sampleFormat="UInt16" colorSpace="RGB" location=")" + std::string(first) + R"("/>)" +
        "<Image " + geometry + R"( sampleFormat="UInt16" colorSpace="RGB" location=")" + std::string(second) +
        R"(" compression="zlib+sh:)" + std::to_string(width * height * channels * 2) + R"(:2"/>)" +
        R"(<Image geometry="2:1:1" sampleFormat="UInt8" location="embedded"><Data encoding="hex">0102</Data></Image>)");
}

// A monolithic unit of the three images, the first two attached.
std::vector<std::byte> unit_of_three_images()
{
    return openxisf::test::file_with_attachments(header_of_three_images("attachment:{0}", "attachment:{1}"),
                                                 image_blocks());
}

// A source over shared bytes that does not declare concurrent reads, and reads them unsynchronized.
std::unique_ptr<openxisf::input_source> serialized_source(std::shared_ptr<const std::vector<std::byte>> data)
{
    const std::uint64_t size = data->size();
    return std::make_unique<openxisf::callback_source>(
        size, [data = std::move(data)](std::uint64_t offset, std::span<std::byte> destination) {
            std::memcpy(destination.data(), data->data() + offset, destination.size());
        });
}

// Every thread reads images at random in both storage models and compares them with the expected bytes. Returns the
// number of wrong reads, failed reads included.
int read_from_threads(const reader& file)
{
    const std::vector<std::byte> planar = samples(pixel_storage::planar);
    const std::vector<std::byte> normal = samples(pixel_storage::normal);
    const std::vector<std::byte> small{std::byte{1}, std::byte{2}};
    std::atomic<int> wrong{0};
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([&, t] {
            openxisf::detail::xoshiro256starstar random({4, 5, 6, static_cast<std::uint64_t>(t) + 1});
            for (int i = 0; i < reads_per_thread; ++i) {
                const std::size_t index = random() % 3;
                const pixel_storage storage = random() % 2 == 0 ? pixel_storage::planar : pixel_storage::normal;
                const std::vector<std::byte>& expected =
                    index == 2 ? small : (storage == pixel_storage::planar ? planar : normal);
                try {
                    if (file.read_pixels(index, {.storage = storage}) != expected) {
                        ++wrong;
                    }
                } catch (const std::exception&) {
                    ++wrong;
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    return wrong.load();
}

TEST(concurrency_pixels, threads_read_the_pixels_of_one_reader_at_once)
{
    const reader file(std::make_unique<openxisf::memory_source>(unit_of_three_images()));
    ASSERT_EQ(file.images().size(), 3U);
    EXPECT_EQ(read_from_threads(file), 0);
}

TEST(concurrency_pixels, a_source_without_concurrent_reads_is_read_one_thread_at_a_time)
{
    const reader file(serialized_source(std::make_shared<const std::vector<std::byte>>(unit_of_three_images())));
    ASSERT_EQ(file.images().size(), 3U);
    EXPECT_EQ(read_from_threads(file), 0);
}

TEST(concurrency_pixels, threads_read_the_external_blocks_of_one_reader_at_once)
{
    // A distributed unit, whose data blocks file the library reads one thread at a time.
    const std::vector<std::vector<std::byte>> data = image_blocks();
    const std::uint64_t second = 112 + data[0].size();
    const auto blocks = std::make_shared<const std::vector<std::byte>>(openxisf::test::blocks_file(
        {{.elements =
              {{.id = 0x1111, .position = 112, .length = data[0].size()},
               {.id = 0x2222, .position = second, .length = data[1].size(), .uncompressed_length = data[0].size()}}}},
        {{.position = 112, .data = data[0]}, {.position = second, .data = data[1]}}));
    const std::string header =
        header_of_three_images("path(@header_dir/unit.xisb):0x1111", "path(@header_dir/unit.xisb):0x2222");
    const reader file(
        std::make_unique<openxisf::memory_source>(openxisf::test::bytes(header)),
        {.resolver = [blocks](const openxisf::external_reference&) { return serialized_source(blocks); }});
    ASSERT_EQ(file.images().size(), 3U);
    ASSERT_TRUE(file.diagnostics().empty());
    EXPECT_EQ(read_from_threads(file), 0);
}

} // namespace
