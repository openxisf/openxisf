// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The samples of group B: the pattern images of samples A4, A2 and A5, compressed by PixInsight with every codec and at
// every item size of byte shuffling that their sample formats have (spec §10.6). Decompressed, each image is byte for
// byte the image of its A sample, which PixInsight wrote without compression.

#include <openxisf/io.h>
#include <openxisf/reader.h>

#include "codec/compression.h"
#include "container/data_block.h"
#include "model/unit.h"
#include "samples/sample_catalog.h"
#include "support/opened_unit.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using openxisf::detail::block_compression;
using openxisf::detail::compression_codec;
using openxisf::detail::unit;
using openxisf::test::sample;

// How PixInsight compressed the image of a sample, and the sample that holds the same image without compression.
struct compressed_image
{
    std::string_view id{};
    std::string_view uncompressed{};
    block_compression compression{};
};

// 37 x 23 pixels: 3 channels of Float32 (A4), 1 of UInt16 (A2) and 1 of Float64 (A5).
constexpr std::uint64_t a4_size = std::uint64_t{37} * 23 * 3 * 4;
constexpr std::uint64_t a2_size = std::uint64_t{37} * 23 * 2;
constexpr std::uint64_t a5_size = std::uint64_t{37} * 23 * 8;

const std::vector<compressed_image>& compressed_images()
{
    static const std::vector<compressed_image> images{
        {.id = "B1",
         .uncompressed = "A4",
         .compression = {.codec = compression_codec::zlib, .uncompressed_size = a4_size}},
        {.id = "B2",
         .uncompressed = "A4",
         .compression = {.codec = compression_codec::zlib, .uncompressed_size = a4_size, .item_size = 4}},
        {.id = "B3",
         .uncompressed = "A4",
         .compression = {.codec = compression_codec::lz4, .uncompressed_size = a4_size}},
        {.id = "B4",
         .uncompressed = "A4",
         .compression = {.codec = compression_codec::lz4, .uncompressed_size = a4_size, .item_size = 4}},
        {.id = "B5",
         .uncompressed = "A4",
         .compression = {.codec = compression_codec::lz4hc, .uncompressed_size = a4_size}},
        {.id = "B6",
         .uncompressed = "A4",
         .compression = {.codec = compression_codec::lz4hc, .uncompressed_size = a4_size, .item_size = 4}},
        {.id = "B7",
         .uncompressed = "A4",
         .compression = {.codec = compression_codec::zstd, .uncompressed_size = a4_size}},
        {.id = "B8",
         .uncompressed = "A4",
         .compression = {.codec = compression_codec::zstd, .uncompressed_size = a4_size, .item_size = 4}},
        {.id = "B9",
         .uncompressed = "A2",
         .compression = {.codec = compression_codec::zstd, .uncompressed_size = a2_size, .item_size = 2}},
        {.id = "B10",
         .uncompressed = "A5",
         .compression = {.codec = compression_codec::zlib, .uncompressed_size = a5_size, .item_size = 8}},
    };
    return images;
}

const compressed_image& image_of(const sample& unit)
{
    for (const compressed_image& image : compressed_images()) {
        if (image.id == unit.id) {
            return image;
        }
    }
    ADD_FAILURE() << "no compressed image for " << unit.id;
    static const compressed_image none{};
    return none;
}

unit open_sample(const sample& unit)
{
    return {std::make_unique<openxisf::file_source>(openxisf::test::sample_path(unit)), {.strict = true}};
}

class samples_group_b : public testing::TestWithParam<sample>
{};

TEST_P(samples_group_b, the_image_decompresses_to_the_image_of_its_uncompressed_sample)
{
    const compressed_image& expected = image_of(GetParam());
    const unit compressed = open_sample(GetParam());
    const unit uncompressed = open_sample(openxisf::test::sample_by_id(expected.uncompressed));

    const openxisf::detail::block_descriptor& image = openxisf::test::descriptor_at(compressed, "/xisf/Image[1]");
    EXPECT_EQ(openxisf::test::value_of(image.compression), expected.compression);
    const std::vector<std::byte> pixels = openxisf::test::block_data(compressed, "/xisf/Image[1]");
    EXPECT_EQ(pixels.size(), expected.compression.uncompressed_size);
    EXPECT_EQ(pixels, openxisf::test::stored_block(uncompressed, "/xisf/Image[1]"));
}

INSTANTIATE_TEST_SUITE_P(pixinsight, samples_group_b, testing::ValuesIn(openxisf::test::samples_of_group('B')),
                         [](const testing::TestParamInfo<sample>& parameter) {
                             return std::string(parameter.param.id);
                         });

} // namespace
