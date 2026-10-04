// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Saves of one writer from several threads at once, which its contract allows: a writer is thread-compatible, so
// const members run concurrently. ThreadSanitizer runs these tests in CI.

#include <openxisf/image.h>
#include <openxisf/io.h>
#include <openxisf/writer.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t thread_count = 8;

TEST(concurrency_writer, saves_of_one_writer_run_at_once_and_agree)
{
    openxisf::image_info image;
    image.geometry = {.dimensions = {64, 48}, .channels = 3};
    image.color_space = openxisf::color_space::rgb;
    std::vector<std::uint16_t> samples(image.geometry.sample_count());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<std::uint16_t>(i * 7);
    }
    for (const bool rewritable : {true, false}) {
        openxisf::writer output({.creator_application = "OpenXISF tests 1.0",
                                 .creation_time = openxisf::date_time{.year = 2026},
                                 .codec = openxisf::codec::zstd,
                                 .byte_shuffle = true,
                                 .subblock_size = 1000,
                                 .checksum = openxisf::checksum_algorithm::sha256});
        output.properties().set("Test:Vector", std::vector<double>(1000, 0.5));
        (void)output.add_image(image, std::span<const std::uint16_t>(samples));

        std::vector<std::vector<std::byte>> files(thread_count);
        std::vector<std::exception_ptr> failures(thread_count);
        std::vector<std::thread> threads;
        threads.reserve(thread_count);
        for (std::size_t t = 0; t < thread_count; ++t) {
            threads.emplace_back([&, t] {
                try {
                    if (rewritable) {
                        openxisf::memory_sink sink;
                        output.save(sink);
                        files[t] = sink.release();
                    } else {
                        openxisf::callback_sink sink([&files, t](std::span<const std::byte> data) {
                            files[t].insert(files[t].end(), data.begin(), data.end());
                        });
                        output.save(sink);
                    }
                } catch (...) {
                    failures[t] = std::current_exception();
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        for (std::size_t t = 0; t < thread_count; ++t) {
            EXPECT_FALSE(failures[t]) << "thread " << t;
            EXPECT_FALSE(files[t].empty());
            EXPECT_EQ(files[t], files[0]) << "thread " << t;
        }
    }
}

} // namespace
