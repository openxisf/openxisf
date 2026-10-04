// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Spec §11.12: Thumbnail elements, on constructed units.

#include <openxisf/error.h>
#include <openxisf/image.h>
#include <openxisf/reader.h>
#include <openxisf/types.h>

#include "codec/compressed_block.h"
#include "codec/compression.h"
#include "support/bytes.h"
#include "support/diagnostics.h"
#include "support/fixture_builder.h"
#include "support/opened_unit.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

using openxisf::color_space;
using openxisf::errc;
using openxisf::reader;
using openxisf::sample_format;
using openxisf::severity;
using openxisf::thumbnail;
using openxisf::test::header_xml;
using openxisf::test::image_xml;
using openxisf::test::no_diagnostics;
using openxisf::test::single_diagnostic;

reader open_body(std::string_view body, openxisf::read_options options = {})
{
    return openxisf::test::open_header(header_xml(body), options);
}

reader open_attached(std::string_view body, const std::vector<std::vector<std::byte>>& blocks,
                     openxisf::read_options options = {})
{
    return openxisf::test::open_unit(openxisf::test::file_with_attachments(header_xml(body), blocks), options);
}

// A thumbnail element with the given attributes and pixel data as hexadecimal digits, in an image.
reader open_thumbnail(std::string_view attributes, std::string_view hex, openxisf::read_options options = {})
{
    return open_body(image_xml({}, "<Thumbnail " + std::string(attributes) +
                                       R"( location="embedded"><Data encoding="hex">)" + std::string(hex) +
                                       "</Data></Thumbnail>"),
                     options);
}

// The only diagnostic is an error with code about the thumbnail, and the image has none.
testing::AssertionResult left_out(const reader& file, errc code)
{
    testing::AssertionResult found =
        single_diagnostic(file.diagnostics(), severity::error, code, "/xisf/Image[1]/Thumbnail[1]");
    if (found && file.image(0).thumbnail) {
        return testing::AssertionFailure() << "the image has a thumbnail";
    }
    return found;
}

TEST(conformance_thumbnail, a_thumbnail_has_its_attributes_and_its_pixel_data)
{
    // The example of spec §11.12, smaller: an RGB UInt8 thumbnail of 2 x 1 pixels, planar.
    const reader file = open_thumbnail(R"(geometry="2:1:3" sampleFormat="UInt8" colorSpace="RGB")", "010203040506");
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
    const thumbnail& small = openxisf::test::value_of(file.image(0).thumbnail);
    EXPECT_EQ(small.geometry, (openxisf::geometry{.dimensions = {2, 1}, .channels = 3}));
    EXPECT_EQ(small.sample_format, sample_format::uint8);
    EXPECT_EQ(small.color_space, color_space::rgb);
    EXPECT_EQ(small.pixel_storage, openxisf::pixel_storage::planar);
    EXPECT_EQ(small.pixels, openxisf::test::bytes("\x01\x02\x03\x04\x05\x06"));
}

TEST(conformance_thumbnail, a_thumbnail_has_the_optional_attributes_of_an_image)
{
    // Spec §11.12: other than its name, a Thumbnail element is an Image element.
    const reader file = open_thumbnail(R"(geometry="2:1:1" sampleFormat="UInt8" colorSpace="Gray" )"
                                       R"(pixelStorage="Normal" imageType="Light" offset="1" orientation="flip" )"
                                       R"(id="small" uuid="c5c93b6d-9072-4e85-9548-1a5391377683")",
                                       "0102");
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
    const thumbnail& small = openxisf::test::value_of(file.image(0).thumbnail);
    EXPECT_EQ(small.pixel_storage, openxisf::pixel_storage::normal);
    EXPECT_EQ(small.image_type, openxisf::image_type::light);
    EXPECT_EQ(small.offset, 1.0);
    EXPECT_EQ(small.orientation, openxisf::orientation::flip);
    EXPECT_EQ(small.id, "small");
    EXPECT_EQ(small.uuid, "c5c93b6d-9072-4e85-9548-1a5391377683");
}

TEST(conformance_thumbnail, the_pixel_data_are_in_native_byte_order)
{
    // UInt16 samples 0x0102 and 0x0304, stored big-endian; then compressed with byte shuffling.
    const reader big = open_attached(image_xml({}, R"(<Thumbnail geometry="2:1:1" sampleFormat="UInt16" )"
                                                   R"(byteOrder="big" location="attachment:{0}"/>)"),
                                     {openxisf::test::bytes("\x01\x02\x03\x04")});
    EXPECT_TRUE(no_diagnostics(big.diagnostics()));
    const std::vector<std::byte>& pixels = openxisf::test::value_of(big.image(0).thumbnail).pixels;
    ASSERT_EQ(pixels.size(), 4U);
    std::uint16_t first = 0;
    std::memcpy(&first, pixels.data(), 2);
    EXPECT_EQ(first, 0x0102U);

    std::vector<std::byte> samples(2000);
    const openxisf::detail::compressed_block block =
        openxisf::detail::compress_block(samples, {.codec = openxisf::detail::compression_codec::lz4, .item_size = 2});
    const reader compressed =
        open_attached(image_xml({}, R"(<Thumbnail geometry="100:10:1" sampleFormat="UInt16" colorSpace="Gray" )"
                                    R"(compression="lz4+sh:2000:2" location="attachment:{0}"/>)"),
                      {block.data});
    EXPECT_TRUE(no_diagnostics(compressed.diagnostics()));
    EXPECT_EQ(openxisf::test::value_of(compressed.image(0).thumbnail).pixels, samples);
}

TEST(conformance_thumbnail, a_thumbnail_is_two_dimensional)
{
    EXPECT_TRUE(left_out(open_thumbnail(R"(geometry="2:1" sampleFormat="UInt8")", "0102"), errc::invalid_thumbnail));
    EXPECT_TRUE(
        left_out(open_thumbnail(R"(geometry="2:1:1:1" sampleFormat="UInt8")", "0102"), errc::invalid_thumbnail));
}

TEST(conformance_thumbnail, a_thumbnail_has_uint8_or_uint16_samples)
{
    const reader uint16 = open_thumbnail(R"(geometry="1:1:1" sampleFormat="UInt16")", "0102");
    EXPECT_TRUE(no_diagnostics(uint16.diagnostics()));
    EXPECT_TRUE(
        left_out(open_thumbnail(R"(geometry="1:1:1" sampleFormat="UInt32")", "01020304"), errc::invalid_thumbnail));
    EXPECT_TRUE(left_out(open_thumbnail(R"(geometry="1:1:1" sampleFormat="Float32" bounds="0:1")", "00000000"),
                         errc::invalid_thumbnail));
}

TEST(conformance_thumbnail, a_thumbnail_is_gray_or_rgb_with_at_most_one_alpha_channel)
{
    const reader gray_alpha = open_thumbnail(R"(geometry="1:1:2" sampleFormat="UInt8" colorSpace="Gray")", "0102");
    EXPECT_TRUE(no_diagnostics(gray_alpha.diagnostics()));
    const reader rgb_alpha = open_thumbnail(R"(geometry="1:1:4" sampleFormat="UInt8" colorSpace="RGB")", "01020304");
    EXPECT_TRUE(no_diagnostics(rgb_alpha.diagnostics()));
    EXPECT_TRUE(left_out(open_thumbnail(R"(geometry="1:1:3" sampleFormat="UInt8" colorSpace="CIELab")", "010203"),
                         errc::invalid_thumbnail));
    EXPECT_TRUE(left_out(open_thumbnail(R"(geometry="1:1:3" sampleFormat="UInt8" colorSpace="Gray")", "010203"),
                         errc::invalid_thumbnail));
}

TEST(conformance_thumbnail, a_thumbnail_has_no_bounds)
{
    // Spec §11.12: its representable range is that of its sample format; bounds are ignored.
    const reader file = open_thumbnail(R"(geometry="1:1:1" sampleFormat="UInt8" bounds="0:1")", "01");
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::invalid_thumbnail,
                                  "/xisf/Image[1]/Thumbnail[1]"));
    EXPECT_EQ(file.diagnostics()[0].context.attribute, "bounds");
    EXPECT_TRUE(file.image(0).thumbnail.has_value());
}

TEST(conformance_thumbnail, a_thumbnail_follows_the_rules_of_images)
{
    EXPECT_TRUE(left_out(open_thumbnail(R"(sampleFormat="UInt8")", "01"), errc::invalid_image));
    EXPECT_TRUE(
        left_out(open_thumbnail(R"(geometry="2:1:1" sampleFormat="UInt8")", "01"), errc::pixel_data_size_mismatch));
    EXPECT_TRUE(left_out(open_body(image_xml({}, R"(<Thumbnail geometry="1:1:1" sampleFormat="UInt8"/>)")),
                         errc::invalid_image));
}

TEST(conformance_thumbnail, a_thumbnail_whose_pixel_data_cannot_be_loaded_is_unavailable)
{
    const reader file =
        open_attached(image_xml({}, R"(<Thumbnail geometry="2:1:1" sampleFormat="UInt8" compression="zlib:2" )"
                                    R"(location="attachment:{0}"/>)"),
                      {openxisf::test::bytes("not zlib")});
    EXPECT_TRUE(left_out(file, errc::corrupt_compressed_data));
    // The image does not depend on its thumbnail.
    EXPECT_EQ(file.images().size(), 1U);
}

TEST(conformance_thumbnail, a_thumbnail_counts_against_the_limit_of_the_data_loaded_at_open)
{
    const std::string body = image_xml({}, R"(<Thumbnail geometry="10:10:1" sampleFormat="UInt8" )"
                                           R"(location="attachment:{0}"/>)");
    const std::vector<std::byte> pixels = openxisf::test::pattern(100);
    EXPECT_TRUE(no_diagnostics(open_attached(body, {pixels}, {.limits = {.max_ancillary_data = 100}}).diagnostics()));
    EXPECT_TRUE(left_out(open_attached(body, {pixels}, {.limits = {.max_ancillary_data = 99}}),
                         errc::ancillary_data_too_large));
}

TEST(conformance_thumbnail, a_thumbnail_has_properties_and_elements_of_its_own)
{
    const reader file = open_body(image_xml(
        {}, openxisf::test::thumbnail_xml({}, R"(<Property id="Thumbnail:P" type="Int32" value="2"/>)"
                                              R"(<Resolution horizontal="10" vertical="10"/>)"
                                              R"(<DisplayFunction m="0.4:0.4:0.4:0.5" s="0:0:0:0" h="1:1:1:1" )"
                                              R"(l="0:0:0:0" r="1:1:1:1"/>)")));
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
    const openxisf::image_info& image = file.image(0);
    EXPECT_FALSE(image.resolution.has_value());
    const thumbnail& small = openxisf::test::value_of(image.thumbnail);
    EXPECT_TRUE(small.properties.contains("Thumbnail:P"));
    EXPECT_EQ(small.resolution, (openxisf::resolution{.horizontal = 10.0, .vertical = 10.0}));
    EXPECT_TRUE(small.display_function.has_value());
}

TEST(conformance_thumbnail, a_thumbnail_has_no_thumbnail)
{
    // Spec §11.12: neither a Thumbnail element nor a Reference to one.
    const reader nested = open_body(image_xml({}, openxisf::test::thumbnail_xml({}, openxisf::test::thumbnail_xml())));
    EXPECT_TRUE(single_diagnostic(nested.diagnostics(), severity::warning, errc::unknown_element,
                                  "/xisf/Image[1]/Thumbnail[1]/Thumbnail[1]"));
    const reader referenced =
        open_body(openxisf::test::thumbnail_xml(R"(uid="other")") +
                  image_xml({}, openxisf::test::thumbnail_xml({}, R"(<Reference ref="other"/>)")));
    EXPECT_TRUE(single_diagnostic(referenced.diagnostics(), severity::warning, errc::invalid_reference,
                                  "/xisf/Image[1]/Thumbnail[1]/Reference[1]"));
    EXPECT_TRUE(referenced.image(0).thumbnail.has_value());
}

TEST(conformance_thumbnail, an_image_has_one_thumbnail)
{
    const reader file = open_body(image_xml({}, openxisf::test::thumbnail_xml(R"(id="first")") +
                                                    openxisf::test::thumbnail_xml(R"(id="second")")));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::duplicate_element,
                                  "/xisf/Image[1]/Thumbnail[2]"));
    EXPECT_EQ(openxisf::test::value_of(file.image(0).thumbnail).id, "first");
}

TEST(conformance_thumbnail, a_standalone_thumbnail_is_not_an_image)
{
    const reader file = open_body(openxisf::test::thumbnail_xml(R"(uid="small")") + image_xml());
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
    ASSERT_EQ(file.images().size(), 1U);
    EXPECT_FALSE(file.image(0).thumbnail.has_value());
}

TEST(conformance_thumbnail, strict_reading_refuses_an_invalid_thumbnail)
{
    EXPECT_TRUE(openxisf::test::throws<openxisf::invalid_data_error>(errc::invalid_thumbnail, [] {
        (void)open_thumbnail(R"(geometry="2:1" sampleFormat="UInt8")", "0102", {.strict = true});
    }));
}

} // namespace
