// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

// The samples of group C: the image of sample A2 with the block checksums that PixInsight writes (spec §10.5), once
// with compression (spec §10.6.1).

#include <openxisf/error.h>
#include <openxisf/io.h>
#include <openxisf/reader.h>

#include "container/block_attributes.h"
#include "container/data_block.h"
#include "model/unit.h"
#include "samples/sample_catalog.h"
#include "support/diagnostics.h"
#include "support/fixture_builder.h"
#include "support/opened_unit.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using openxisf::errc;
using openxisf::severity;
using openxisf::detail::unit;
using openxisf::test::sample;
using openxisf::test::sample_by_id;

std::vector<std::byte> read_sample(const sample& unit)
{
    const openxisf::file_source file(openxisf::test::sample_path(unit));
    std::vector<std::byte> data(file.size());
    file.read(0, data);
    return data;
}

// The samples whose image is not compressed.
std::vector<sample> uncompressed_samples()
{
    std::vector<sample> found;
    for (const std::string_view id : {"C1", "C2", "C3"}) {
        found.push_back(sample_by_id(id));
    }
    return found;
}

std::string sample_name(const testing::TestParamInfo<sample>& parameter)
{
    return std::string(parameter.param.id);
}

// The offset in a file of a character in the middle of the Base64 text of its first Data element.
std::size_t inside_first_data_element(const std::vector<std::byte>& file)
{
    const std::string_view text(reinterpret_cast<const char*>(file.data()), file.size());
    const std::size_t data = text.find("<Data ");
    return text.find('>', data) + 100;
}

// A changed Base64 digit is still a digit, and changes the decoded bytes.
void change_digit(std::byte& digit)
{
    digit = digit == std::byte{'A'} ? std::byte{'B'} : std::byte{'A'};
}

class samples_group_c : public testing::TestWithParam<sample>
{};

class samples_group_c_uncompressed : public testing::TestWithParam<sample>
{};

// The pattern image of the samples (sample_catalog.cpp): sample i, in storage order, is i/850 of the range of UInt16,
// as PixInsight rounds it. The image of C6 is decompressed.
TEST_P(samples_group_c, the_image_block_holds_the_pattern)
{
    const unit opened(std::make_unique<openxisf::file_source>(openxisf::test::sample_path(GetParam())), {});
    const std::vector<std::byte> pixels = openxisf::test::block_data(opened, "/xisf/Image[1]");

    ASSERT_EQ(pixels.size(), std::size_t{37} * 23 * 2);
    for (std::size_t i = 0; i < pixels.size() / 2; ++i) {
        const auto value = static_cast<double>(std::to_integer<unsigned int>(pixels[2 * i]) |
                                               (std::to_integer<unsigned int>(pixels[(2 * i) + 1]) << 8U));
        const double expected = std::round(static_cast<double>(i) / 850.0 * 65535.0);
        EXPECT_LE(std::abs(value - expected), 1.0) << "sample " << i;
    }
}

TEST_P(samples_group_c_uncompressed, a_change_in_the_embedded_image_fails_its_checksum)
{
    std::vector<std::byte> file = read_sample(GetParam());
    change_digit(file[inside_first_data_element(file)]);

    const unit opened = openxisf::test::open_internal(file);
    EXPECT_TRUE(openxisf::test::single_diagnostic(openxisf::test::unexpected_diagnostics(opened.diagnostics),
                                                  severity::error, errc::checksum_mismatch, "/xisf/Image[1]/Data[1]"));
    EXPECT_TRUE(openxisf::test::throws<openxisf::integrity_error>(
        errc::checksum_mismatch, [&file] { (void)openxisf::test::open_unit(file, {.strict = true}); }));
}

INSTANTIATE_TEST_SUITE_P(pixinsight, samples_group_c, testing::ValuesIn(openxisf::test::samples_of_group('C')),
                         sample_name);
INSTANTIATE_TEST_SUITE_P(pixinsight, samples_group_c_uncompressed, testing::ValuesIn(uncompressed_samples()),
                         sample_name);

// Spec §10.6.1: the checksum of a compressed block covers the compressed data.
TEST(samples_c6, the_checksum_of_a_compressed_block_covers_the_stored_bytes)
{
    const unit opened(std::make_unique<openxisf::file_source>(openxisf::test::sample_path(sample_by_id("C6"))),
                      {.strict = true});
    const openxisf::detail::block_descriptor& image = openxisf::test::descriptor_at(opened, "/xisf/Image[1]");
    const openxisf::detail::block_compression& compression = openxisf::test::value_of(image.compression);
    EXPECT_EQ(compression.codec, openxisf::detail::compression_codec::zstd);
    EXPECT_EQ(compression.uncompressed_size, 1702U);
    EXPECT_EQ(compression.item_size, 2U);
    EXPECT_EQ(openxisf::test::value_of(image.checksum).algorithm, openxisf::detail::hash_algorithm::sha256);
    // The open verified the digest against these bytes: the compressed ones, which start with a Zstandard frame.
    const std::vector<std::byte> stored = openxisf::test::stored_block(opened, "/xisf/Image[1]");
    ASSERT_EQ(stored.size(), 1277U);
    EXPECT_EQ(stored[0], std::byte{0x28});
    EXPECT_EQ(stored[1], std::byte{0xB5});
    EXPECT_EQ(stored[2], std::byte{0x2F});
    EXPECT_EQ(stored[3], std::byte{0xFD});
}

TEST(samples_c6, a_change_in_a_compressed_image_fails_its_checksum_before_decompression)
{
    std::vector<std::byte> file = read_sample(sample_by_id("C6"));
    change_digit(file[inside_first_data_element(file)]);

    const unit opened = openxisf::test::open_internal(file);
    EXPECT_TRUE(openxisf::test::single_diagnostic(openxisf::test::unexpected_diagnostics(opened.diagnostics),
                                                  severity::error, errc::checksum_mismatch, "/xisf/Image[1]/Data[1]"));
    EXPECT_TRUE(openxisf::test::throws<openxisf::integrity_error>(
        errc::checksum_mismatch, [&opened] { (void)openxisf::test::stored_block(opened, "/xisf/Image[1]"); }));
}

TEST(samples_c6, a_change_in_an_attached_compressed_block_fails_its_checksum)
{
    // The thumbnail of C6, compressed with zstd, is attached at byte 4332. Its pixel data are loaded with the unit,
    // which finds the change; without the ancillary data, the read of the block finds it.
    std::vector<std::byte> file = read_sample(sample_by_id("C6"));
    file[4332 + 100] ^= std::byte{0x01};

    const unit opened = openxisf::test::open_internal(file);
    EXPECT_TRUE(openxisf::test::single_diagnostic(openxisf::test::unexpected_diagnostics(opened.diagnostics),
                                                  severity::error, errc::checksum_mismatch,
                                                  "/xisf/Image[1]/Thumbnail[1]"));
    ASSERT_EQ(opened.images.infos.size(), 1U);
    EXPECT_FALSE(opened.images.infos[0].thumbnail.has_value());

    const unit header_only = openxisf::test::open_internal(file, {.header_only = true});
    EXPECT_TRUE(openxisf::test::no_diagnostics(openxisf::test::unexpected_diagnostics(header_only.diagnostics)));
    EXPECT_TRUE(openxisf::test::throws<openxisf::integrity_error>(errc::checksum_mismatch, [&header_only] {
        (void)openxisf::test::stored_block(header_only, "/xisf/Image[1]/Thumbnail[1]");
    }));
}

} // namespace
