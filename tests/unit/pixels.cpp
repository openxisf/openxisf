// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "model/pixels.h"

#include <openxisf/error.h>
#include <openxisf/io.h>
#include <openxisf/reader.h>

#include "codec/codecs.h"
#include "model/unit.h"
#include "support/bytes.h"
#include "support/faulty_io.h"
#include "support/fixture_builder.h"
#include "support/opened_unit.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace {

using openxisf::errc;
using openxisf::detail::unit;

// A gray 8-bit image of 10 pixels in an attached block.
unit open_attached_image()
{
    return openxisf::test::open_internal(openxisf::test::file_with_attachments(
        openxisf::test::header_xml(R"(<Image geometry="10:1:1" sampleFormat="UInt8" location="attachment:{0}"/>)"),
        {openxisf::test::pattern(10)}));
}

TEST(pixels, progress_is_reported_after_each_piece_read)
{
    const unit opened = open_attached_image();
    std::vector<std::pair<std::uint64_t, std::uint64_t>> calls;
    const openxisf::pixel_read_options options{.progress = [&calls](std::uint64_t done, std::uint64_t total) {
        calls.emplace_back(done, total);
        return true;
    }};
    EXPECT_EQ(openxisf::detail::read_pixels(opened, 0, options, 4), openxisf::test::pattern(10));
    EXPECT_EQ(calls, (std::vector<std::pair<std::uint64_t, std::uint64_t>>{{0, 10}, {4, 10}, {8, 10}, {10, 10}}));
}

TEST(pixels, a_read_cancelled_between_pieces_stops_there)
{
    const unit opened = open_attached_image();
    int calls = 0;
    const openxisf::pixel_read_options options{
        .progress = [&calls](std::uint64_t, std::uint64_t) { return ++calls < 3; }};
    std::vector<std::byte> destination(10, std::byte{0xEE});
    EXPECT_TRUE(openxisf::test::throws<openxisf::cancelled_error>(
        errc::cancelled, [&] { openxisf::detail::read_pixels(opened, 0, destination, options, 4); }));
    EXPECT_EQ(calls, 3);
    // Two pieces were read, straight into the destination; the last one was not.
    std::vector<std::byte> expected(10, std::byte{0xEE});
    std::ranges::copy(openxisf::test::pattern(8), expected.begin());
    EXPECT_EQ(destination, expected);
}

TEST(pixels, data_that_keep_their_layout_are_read_straight_into_the_destination)
{
    // The source records where it was asked to write.
    const std::vector<std::byte> file = openxisf::test::file_with_attachments(
        openxisf::test::header_xml(R"(<Image geometry="10:1:1" sampleFormat="UInt8" location="attachment:{0}"/>)"),
        {openxisf::test::pattern(10)});
    std::vector<std::span<std::byte>> reads;
    auto source = std::make_unique<openxisf::callback_source>(
        file.size(), [&file, &reads](std::uint64_t offset, std::span<std::byte> destination) {
            reads.push_back(destination);
            std::copy_n(file.begin() + static_cast<std::ptrdiff_t>(offset), destination.size(), destination.begin());
        });
    const unit opened(std::move(source), {});
    reads.clear();

    std::vector<std::byte> destination(10);
    openxisf::detail::read_pixels(opened, 0, destination, {});
    EXPECT_EQ(destination, openxisf::test::pattern(10));
    ASSERT_EQ(reads.size(), 1U);
    EXPECT_EQ(reads.front().data(), destination.data());
    EXPECT_EQ(reads.front().size(), destination.size());
}

// Success when read() throws what a faulty source throws for a failure of kind.
testing::AssertionResult failed_with(openxisf::test::fault kind, const std::function<void()>& read)
{
    using openxisf::test::fault;
    switch (kind) {
    case fault::error:
        return openxisf::test::throws<openxisf::io_error>(errc::read_failed, read);
    case fault::short_transfer:
        return openxisf::test::throws<openxisf::io_error>(errc::end_of_data, read);
    case fault::foreign_exception:
        break;
    }
    try {
        read();
    } catch (const openxisf::test::injected_fault&) {
        return testing::AssertionSuccess();
    } catch (const std::exception& other) {
        return testing::AssertionFailure() << "threw another exception: " << other.what();
    }
    return testing::AssertionFailure() << "threw nothing";
}

TEST(pixels, a_failure_of_the_source_stops_a_read_there_and_passes_through)
{
    // An attached image of 10 pixels and a compressed one, read in pieces of 4 bytes into a vector or into memory of
    // the caller, from a source whose read of each piece in turn fails in each way: the read throws what the source
    // threw, and reads nothing after it.
    using openxisf::test::fault;
    using openxisf::test::faulty_source;
    std::vector<std::byte> compressed;
    openxisf::detail::zlib_compress(openxisf::test::pattern(10), 6, compressed);
    const openxisf::memory_source file(openxisf::test::file_with_attachments(
        openxisf::test::header_xml(
            R"(<Image geometry="10:1:1" sampleFormat="UInt8" location="attachment:{0}"/>)"
            R"(<Image geometry="10:1:1" sampleFormat="UInt8" compression="zlib:10" location="attachment:{1}"/>)"),
        {openxisf::test::pattern(10), compressed}));
    const openxisf::pixel_read_options options{.progress = [](std::uint64_t, std::uint64_t) { return true; }};
    // The reads of the open: the first 16 bytes and the header.
    constexpr std::size_t open_reads = 2;
    for (const std::size_t index : {0U, 1U}) {
        const std::size_t pieces = index == 0 ? 3 : (compressed.size() + 3) / 4;
        for (const bool into_vector : {true, false}) {
            for (const fault kind : {fault::error, fault::short_transfer, fault::foreign_exception}) {
                for (std::size_t piece = 1; piece <= pieces; ++piece) {
                    auto source = std::make_unique<faulty_source>(file, open_reads + piece, kind);
                    const faulty_source& faulty = *source;
                    const unit opened(std::move(source), {});
                    std::vector<std::byte> destination(10);
                    EXPECT_TRUE(
                        failed_with(kind,
                                    [&] {
                                        if (into_vector) {
                                            destination = openxisf::detail::read_pixels(opened, index, options, 4);
                                        } else {
                                            openxisf::detail::read_pixels(opened, index, destination, options, 4);
                                        }
                                    }))
                        << "image " << index << ", piece " << piece;
                    EXPECT_EQ(faulty.reads(), open_reads + piece) << "image " << index << ", piece " << piece;
                }
            }
        }
    }
}

TEST(pixels, an_unavailable_block_throws_its_error_before_any_progress)
{
    const unit opened = openxisf::test::open_internal(openxisf::test::file_with_attachments(
        openxisf::test::header_xml(
            R"(<Image geometry="2:1:1" sampleFormat="UInt8" compression="rle:2" location="attachment:{0}"/>)"),
        {openxisf::test::bytes("ab")}));
    int calls = 0;
    const openxisf::pixel_read_options options{.progress = [&calls](std::uint64_t, std::uint64_t) {
        ++calls;
        return true;
    }};
    EXPECT_TRUE(openxisf::test::throws<openxisf::unsupported_error>(
        errc::unsupported_compression, [&] { (void)openxisf::detail::read_pixels(opened, 0, options); }));
    EXPECT_EQ(calls, 0);
}

} // namespace
