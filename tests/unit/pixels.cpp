// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "model/pixels.h"

#include <openxisf/error.h>
#include <openxisf/reader.h>

#include "model/unit.h"
#include "support/bytes.h"
#include "support/fixture_builder.h"
#include "support/opened_unit.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
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

TEST(pixels, a_read_cancelled_between_pieces_leaves_the_destination_alone)
{
    const unit opened = open_attached_image();
    int calls = 0;
    const openxisf::pixel_read_options options{
        .progress = [&calls](std::uint64_t, std::uint64_t) { return ++calls < 3; }};
    std::vector<std::byte> destination(10, std::byte{0xEE});
    EXPECT_TRUE(openxisf::test::throws<openxisf::cancelled_error>(
        errc::cancelled, [&] { openxisf::detail::read_pixels(opened, 0, destination, options, 4); }));
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(destination, std::vector<std::byte>(10, std::byte{0xEE}));
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
