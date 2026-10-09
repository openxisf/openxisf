// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Saves from several threads at once: of one writer, which its contract allows, since a writer is thread-compatible and
// const members run concurrently, and of a writer for each thread, which share their pixel data. Each thread saves the
// bytes of a save on one thread, which reads back as the model. ThreadSanitizer runs these tests in CI.

#include <openxisf/image.h>
#include <openxisf/io.h>
#include <openxisf/reader.h>
#include <openxisf/writer.h>

#include "support/fixture_builder.h"
#include "support/threads.h"
#include "support/written_unit.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <vector>

namespace {

constexpr std::size_t thread_count = 8;

// An RGB UInt16 image of 64 × 48 pixels and its samples.
struct image_model
{
    openxisf::image_info image{};
    std::vector<std::uint16_t> samples{};

    image_model()
    {
        image.geometry = {.dimensions = {64, 48}, .channels = 3};
        image.color_space = openxisf::color_space::rgb;
        samples.resize(image.geometry.sample_count());
        for (std::size_t i = 0; i < samples.size(); ++i) {
            samples[i] = static_cast<std::uint16_t>(i * 7);
        }
    }

    // A writer of the image, with a vector property, compressed in subblocks and hashed. It borrows the samples.
    [[nodiscard]] openxisf::writer writer() const
    {
        openxisf::writer output({.creator_application = "OpenXISF tests 1.0",
                                 .creation_time = openxisf::date_time{.year = 2026},
                                 .codec = openxisf::codec::zstd,
                                 .byte_shuffle = true,
                                 .subblock_size = 1000,
                                 .checksum = openxisf::checksum_algorithm::sha256});
        output.properties().set("Test:Vector", std::vector<double>(1000, 0.5));
        (void)output.add_image(image, std::span<const std::uint16_t>(samples));
        return output;
    }
};

// A unit saved by output into a sink that can rewrite, or into one that receives the unit in order.
std::vector<std::byte> saved(const openxisf::writer& output, bool rewritable)
{
    if (rewritable) {
        openxisf::memory_sink sink;
        output.save(sink);
        return sink.release();
    }
    std::vector<std::byte> file;
    openxisf::callback_sink sink(
        [&file](std::span<const std::byte> data) { file.insert(file.end(), data.begin(), data.end()); });
    output.save(sink);
    return file;
}

// What each thread saved, and how it failed.
struct results
{
    std::vector<std::vector<std::byte>> files = std::vector<std::vector<std::byte>>(thread_count);
    std::vector<std::exception_ptr> failures = std::vector<std::exception_ptr>(thread_count);

    // Every thread saved, without failing, the bytes of a save on one thread, expected.
    void expect_agreement(const std::vector<std::byte>& expected) const
    {
        for (std::size_t t = 0; t < thread_count; ++t) {
            EXPECT_FALSE(failures[t]) << "thread " << t;
            EXPECT_EQ(files[t], expected) << "thread " << t;
        }
    }
};

// The unit saved on one thread holds the model: it reads back as its pixels and properties.
void expect_model(const std::vector<std::byte>& file, const image_model& model, const openxisf::writer& output)
{
    const openxisf::reader unit = openxisf::test::open_unit(file, {.strict = true});
    EXPECT_EQ(unit.read_pixels<std::uint16_t>(0), model.samples);
    EXPECT_EQ(unit.properties(), output.properties());
}

TEST(concurrency_writer, saves_of_one_writer_run_at_once_and_agree)
{
    const image_model model;
    const openxisf::writer output = model.writer();
    for (const bool rewritable : {true, false}) {
        results saves;
        openxisf::test::run_threads(thread_count, [&](std::size_t t) {
            try {
                saves.files[t] = saved(output, rewritable);
            } catch (...) {
                saves.failures[t] = std::current_exception();
            }
        });
        const std::vector<std::byte> alone = saved(output, rewritable);
        saves.expect_agreement(alone);
        expect_model(alone, model, output);
    }
}

TEST(concurrency_writer, writers_of_their_own_save_at_once_and_agree)
{
    // Each thread builds its writer, over pixel data that all of them share.
    const image_model model;
    for (const bool rewritable : {true, false}) {
        results saves;
        openxisf::test::run_threads(thread_count, [&](std::size_t t) {
            try {
                saves.files[t] = saved(model.writer(), rewritable);
            } catch (...) {
                saves.failures[t] = std::current_exception();
            }
        });
        const openxisf::writer output = model.writer();
        const std::vector<std::byte> alone = saved(output, rewritable);
        saves.expect_agreement(alone);
        expect_model(alone, model, output);
    }
}

TEST(concurrency_writer, distributed_saves_of_one_writer_run_at_once)
{
    // The identifiers of the blocks are random, so the units differ; each reads back as the model.
    const image_model model;
    const openxisf::writer output = model.writer();
    std::vector<openxisf::test::distributed_unit> units(thread_count);
    std::vector<std::exception_ptr> failures(thread_count);
    openxisf::test::run_threads(thread_count, [&](std::size_t t) {
        try {
            openxisf::memory_sink header;
            openxisf::memory_sink blocks;
            output.save_distributed(header, blocks, openxisf::test::blocks_file_name);
            units[t] = {.header = header.release(), .blocks = blocks.release()};
        } catch (...) {
            failures[t] = std::current_exception();
        }
    });
    for (std::size_t t = 0; t < thread_count; ++t) {
        EXPECT_FALSE(failures[t]) << "thread " << t;
        const openxisf::reader unit = openxisf::test::open_distributed(units[t], {.strict = true});
        EXPECT_EQ(unit.read_pixels<std::uint16_t>(0), model.samples) << "thread " << t;
        EXPECT_EQ(unit.properties(), output.properties()) << "thread " << t;
    }
}

} // namespace
