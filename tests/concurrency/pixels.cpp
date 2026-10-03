// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

// Pixel reads from 16 threads at once on one reader, whose images are attached, compressed and embedded, on a source
// that supports concurrent reads and on one that the library serializes. ThreadSanitizer runs these tests in CI.

#include <openxisf/io.h>
#include <openxisf/reader.h>
#include <openxisf/types.h>

#include "codec/compressed_block.h"
#include "codec/compression.h"
#include "core/xoshiro.h"
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

// A unit of three images: the planar samples attached, in little-endian byte order, the same compressed with zlib and
// byte shuffling, and an image of two pixels embedded.
std::vector<std::byte> unit_of_three_images()
{
    std::vector<std::byte> planar;
    for (const std::uint16_t value : values(pixel_storage::planar)) {
        planar.push_back(static_cast<std::byte>(value & 0xFFU));
        planar.push_back(static_cast<std::byte>(value >> 8U));
    }
    const openxisf::detail::compressed_block compressed =
        openxisf::detail::compress_block(planar, {.codec = openxisf::detail::compression_codec::zlib, .item_size = 2});
    const std::string geometry = R"(geometry=")" + std::to_string(width) + ":" + std::to_string(height) + R"(:3")";
    return openxisf::test::file_with_attachments(
        openxisf::test::header_xml(
            "<Image " + geometry + R"( sampleFormat="UInt16" colorSpace="RGB" location="attachment:{0}"/>)" +
            "<Image " + geometry + R"( sampleFormat="UInt16" colorSpace="RGB" location="attachment:{1}" )" +
            R"(compression="zlib+sh:)" + std::to_string(planar.size()) + R"(:2"/>)" +
            R"(<Image geometry="2:1:1" sampleFormat="UInt8" location="embedded"><Data encoding="hex">0102</Data></Image>)"),
        {planar, compressed.data});
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
    // A callback source that does not declare concurrent reads, over a buffer that it reads unsynchronized.
    const auto unit = std::make_shared<const std::vector<std::byte>>(unit_of_three_images());
    const reader file(std::make_unique<openxisf::callback_source>(
        unit->size(), [unit](std::uint64_t offset, std::span<std::byte> destination) {
            std::memcpy(destination.data(), unit->data() + offset, destination.size());
        }));
    ASSERT_EQ(file.images().size(), 3U);
    EXPECT_EQ(read_from_threads(file), 0);
}

} // namespace
