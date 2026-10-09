// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Every sample written by PixInsight, read, written again by the writer, and read back: what the reader returns is a
// model that the writer accepts as it is, and the unit written from it holds the same images, properties and pixels.
// That holds for a unit read without warnings, as the samples are, unless it lists an image again or holds a TimePoint
// beyond the years 0 to 9999 in UTC (writer.h). A warning may come with something that the reader keeps and the writer
// refuses, such as a blank FITS keyword, which the caller drops before writing the model again.

#include <openxisf/color.h>
#include <openxisf/image.h>
#include <openxisf/property.h>
#include <openxisf/reader.h>
#include <openxisf/writer.h>

#include "model/ancillary_attributes.h"
#include "samples/sample_catalog.h"
#include "support/diagnostics.h"
#include "support/fixture_builder.h"
#include "support/written_unit.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace {

using openxisf::test::sample;
using openxisf::test::sink_kind;

// What the writer changes: the embedded flag of an ICC profile is set, and the luminance coefficients of a working
// space are those of its chromaticities (spec §11.7, §11.8.1).
template <typename Image> void as_written(Image& image)
{
    if (!image.icc_profile.empty()) {
        image.icc_profile[47] |= std::byte{0x01};
    }
    if (image.rgb_working_space) {
        openxisf::rgb_working_space& space = *image.rgb_working_space;
        space.luminance = openxisf::luminance_coefficients(space.x, space.y).value_or(space.luminance);
    }
}

class samples_rewrite : public testing::TestWithParam<sample>
{};

TEST_P(samples_rewrite, reads_back_as_written_by_pixinsight)
{
    const openxisf::reader original(openxisf::test::sample_path(GetParam()));
    std::vector<std::vector<std::byte>> pixels;
    for (std::size_t i = 0; i < original.images().size(); ++i) {
        pixels.push_back(original.read_pixels(i));
    }

    struct variant
    {
        std::optional<openxisf::codec> codec{};
        std::optional<openxisf::checksum_algorithm> checksum{};
        sink_kind sink = sink_kind::rewritable;
    };
    for (const variant& tested :
         {variant{}, variant{.codec = openxisf::codec::zstd, .checksum = openxisf::checksum_algorithm::sha256},
          variant{.codec = openxisf::codec::lz4hc,
                  .checksum = openxisf::checksum_algorithm::sha1,
                  .sink = sink_kind::append_only}}) {
        openxisf::writer output({.creator_application = "OpenXISF tests 1.0",
                                 .creation_time = openxisf::date_time{.year = 2026, .month = 10, .day = 4},
                                 .codec = tested.codec,
                                 .byte_shuffle = true,
                                 .checksum = tested.checksum});
        output.metadata() = original.metadata();
        output.properties() = original.properties();
        output.tables().assign(original.tables().begin(), original.tables().end());
        for (std::size_t i = 0; i < original.images().size(); ++i) {
            (void)output.add_image(original.image(i), pixels[i]);
        }

        const openxisf::reader unit =
            openxisf::test::open_unit(openxisf::test::written(output, tested.sink), {.strict = true});
        EXPECT_TRUE(openxisf::test::no_diagnostics(unit.diagnostics()));
        ASSERT_EQ(unit.images().size(), original.images().size());
        for (std::size_t i = 0; i < original.images().size(); ++i) {
            openxisf::image_info expected = original.image(i);
            as_written(expected);
            if (expected.thumbnail) {
                as_written(*expected.thumbnail);
            }
            EXPECT_EQ(unit.image(i), expected) << "image " << i;
            EXPECT_EQ(unit.read_pixels(i), pixels[i]) << "image " << i;
        }
        EXPECT_EQ(unit.properties(), original.properties());
        EXPECT_TRUE(std::ranges::equal(unit.tables(), original.tables()));
        for (const openxisf::property& item : original.metadata()) {
            if (!item.id.starts_with("XISF:Creat") && item.id != "XISF:BlockAlignmentSize" &&
                item.id != "XISF:MaxInlineBlockSize" && !item.id.starts_with("XISF:Compression") &&
                item.id != "XISF:ChecksumAlgorithms") {
                EXPECT_EQ(unit.metadata().at(item.id), item);
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(pixinsight, samples_rewrite, testing::ValuesIn(openxisf::test::all_samples()),
                         [](const testing::TestParamInfo<sample>& parameter) {
                             return std::string(parameter.param.id);
                         });

} // namespace
