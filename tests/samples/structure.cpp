// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Every sample written by PixInsight opens as written: the elements of its header, and its data blocks.

#include <openxisf/image.h>
#include <openxisf/io.h>
#include <openxisf/property.h>
#include <openxisf/reader.h>
#include <openxisf/types.h>

#include "container/data_block.h"
#include "container/file_layout.h"
#include "core/diagnostic_log.h"
#include "core/utc_time.h"
#include "io/thread_safe_source.h"
#include "model/header.h"
#include "model/outline.h"
#include "model/unit.h"
#include "samples/sample_catalog.h"
#include "support/diagnostics.h"
#include "support/opened_unit.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

using openxisf::test::sample;

class samples_structure : public testing::TestWithParam<sample>
{};

TEST_P(samples_structure, opens_with_a_warning_about_the_type_of_its_creation_time)
{
    const openxisf::reader file(openxisf::test::sample_path(GetParam()));

    EXPECT_EQ(file.storage(), openxisf::unit_storage::monolithic);
    EXPECT_EQ(file.signature(), openxisf::signature_status::none);
    EXPECT_TRUE(openxisf::test::single_diagnostic(file.diagnostics(), openxisf::severity::warning,
                                                  openxisf::errc::reserved_property_type,
                                                  openxisf::test::creation_time_path));
    EXPECT_EQ(file.metadata().at("XISF:CreationTime").value.type(), openxisf::property_type::string);
}

TEST_P(samples_structure, has_the_metadata_of_pixinsight)
{
    const openxisf::reader file(openxisf::test::sample_path(GetParam()));
    const openxisf::property_list& metadata = file.metadata();
    EXPECT_EQ(metadata.at("XISF:CreatorApplication").value, openxisf::property_value("PixInsight 1.9.4"));
    EXPECT_EQ(metadata.at("XISF:CreatorModule").value, openxisf::property_value("XISF module version 1.1.3"));
    EXPECT_EQ(metadata.at("XISF:CreatorOS").value, openxisf::property_value("Windows"));
    // The String of the creation time holds a TimePoint of the day the samples were written.
    const openxisf::date_time created =
        openxisf::detail::parse_time_point(metadata.at("XISF:CreationTime").value.get<std::string>());
    EXPECT_EQ(created.year, 2026);
}

TEST_P(samples_structure, has_the_elements_that_pixinsight_wrote)
{
    const openxisf::detail::thread_safe_source source(
        std::make_unique<openxisf::file_source>(openxisf::test::sample_path(GetParam())));
    openxisf::detail::diagnostic_log log(true);
    const openxisf::detail::unit_header found = openxisf::detail::read_unit_header(source, {}, log);
    const openxisf::detail::parsed_header header = openxisf::detail::parse_header(found.text, found.offset, {}, log);
    const openxisf::detail::unit_outline outline = openxisf::detail::build_outline(header.root, log);

    std::vector<std::string> paths;
    paths.reserve(outline.elements.size());
    for (std::size_t i = 0; i < outline.elements.size(); ++i) {
        paths.push_back(outline.path(i));
    }
    EXPECT_EQ(paths, GetParam().elements);
}

TEST_P(samples_structure, every_data_block_reads_passes_its_checksum_and_decompresses)
{
    const openxisf::detail::unit opened(
        std::make_unique<openxisf::file_source>(openxisf::test::sample_path(GetParam())), {.strict = true});

    EXPECT_EQ(openxisf::test::block_paths(opened), GetParam().blocks);
    for (const openxisf::detail::data_block& block : opened.blocks) {
        const openxisf::detail::block_descriptor& descriptor = openxisf::test::descriptor_at(opened, block.path);
        std::vector<std::byte> data;
        EXPECT_NO_THROW(data = openxisf::detail::read_block(opened.source, block, opened.limits)) << block.path;
        if (descriptor.compression) {
            EXPECT_EQ(data.size(), descriptor.compression->uncompressed_size) << block.path;
        }
    }
}

TEST_P(samples_structure, every_image_has_the_thumbnail_icc_profile_and_resolution_that_pixinsight_wrote)
{
    const openxisf::detail::unit opened(
        std::make_unique<openxisf::file_source>(openxisf::test::sample_path(GetParam())), {.strict = true});
    const std::vector<std::string>& blocks = GetParam().blocks;
    const auto has_block = [&blocks](const std::string& path) {
        return std::ranges::find(blocks, path) != blocks.end();
    };
    for (std::size_t i = 0; i < opened.images.infos.size(); ++i) {
        const openxisf::image_info& info = opened.images.infos[i];
        const std::string& path = opened.blocks[opened.images.blocks[i]].path;
        // PixInsight gives every image a resolution, 72 pixels per inch unless the image window has another.
        EXPECT_TRUE(info.resolution.has_value()) << path;

        EXPECT_EQ(info.thumbnail.has_value(), has_block(path + "/Thumbnail[1]")) << path;
        if (info.thumbnail) {
            const openxisf::thumbnail& small = *info.thumbnail;
            EXPECT_EQ(small.sample_format, openxisf::sample_format::uint8) << path;
            EXPECT_EQ(small.geometry.dimensions.size(), 2U) << path;
            EXPECT_EQ(small.pixels.size(), small.geometry.sample_count()) << path;
        }

        // An ICC profile starts with its size, and has the signature acsp at byte 36 (ICC.1, section 7.2).
        EXPECT_EQ(info.icc_profile.empty(), !has_block(path + "/ICCProfile[1]")) << path;
        if (info.icc_profile.size() >= 40) {
            const std::vector<std::byte>& profile = info.icc_profile;
            const auto byte_at = [&profile](std::size_t at) { return std::to_integer<std::uint32_t>(profile[at]); };
            EXPECT_EQ((byte_at(0) << 24U) | (byte_at(1) << 16U) | (byte_at(2) << 8U) | byte_at(3), profile.size());
            std::string signature;
            for (std::size_t j = 36; j < 40; ++j) {
                signature += static_cast<char>(profile[j]);
            }
            EXPECT_EQ(signature, "acsp") << path;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(pixinsight, samples_structure, testing::ValuesIn(openxisf::test::all_samples()),
                         [](const testing::TestParamInfo<sample>& parameter) {
                             return std::string(parameter.param.id);
                         });

} // namespace
