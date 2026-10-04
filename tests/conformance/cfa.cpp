// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Spec §11.10: ColorFilterArray elements, on constructed units.

#include <openxisf/error.h>
#include <openxisf/image.h>
#include <openxisf/reader.h>

#include "support/diagnostics.h"
#include "support/fixture_builder.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>

namespace {

using openxisf::color_filter_array;
using openxisf::errc;
using openxisf::reader;
using openxisf::severity;
using openxisf::test::header_xml;
using openxisf::test::image_xml;
using openxisf::test::no_diagnostics;
using openxisf::test::single_diagnostic;

reader open_filter(std::string_view attributes, openxisf::read_options options = {})
{
    return openxisf::test::open_header(header_xml(image_xml({}, "<ColorFilterArray " + std::string(attributes) + "/>")),
                                       std::move(options));
}

testing::AssertionResult unavailable(const reader& file)
{
    testing::AssertionResult found = single_diagnostic(
        file.diagnostics(), severity::error, errc::invalid_color_filter_array, "/xisf/Image[1]/ColorFilterArray[1]");
    if (found && file.image(0).color_filter_array) {
        return testing::AssertionFailure() << "the image has a colour filter array";
    }
    return found;
}

TEST(conformance_cfa, the_examples_of_the_specification_are_read)
{
    const reader bayer = open_filter(R"(pattern="GRBG" width="2" height="2" name="GRBG Bayer Filter")");
    EXPECT_TRUE(no_diagnostics(bayer.diagnostics()));
    EXPECT_EQ(bayer.image(0).color_filter_array,
              (color_filter_array{.pattern = "GRBG", .width = 2, .height = 2, .name = "GRBG Bayer Filter"}));

    const reader xtrans = open_filter(R"(pattern="GBGGRGRGRBGBGBGGRGGRGGBGBGBRGRGRGGBG" width="6" height="6" )"
                                      R"(name="Fujifilm X-Trans Filter")");
    EXPECT_TRUE(no_diagnostics(xtrans.diagnostics()));
    EXPECT_EQ(xtrans.image(0).color_filter_array.value_or(color_filter_array{}).width, 6U);

    // Without the recommended name.
    const reader kodak = open_filter(R"(pattern="GWRWWGWRBWGWWBWG" width="4" height="4")");
    EXPECT_TRUE(no_diagnostics(kodak.diagnostics()));
    EXPECT_EQ(kodak.image(0).color_filter_array,
              (color_filter_array{.pattern = "GWRWWGWRBWGWWBWG", .width = 4, .height = 4}));
}

TEST(conformance_cfa, every_element_of_table_18_can_be_in_a_pattern)
{
    const reader file = open_filter(R"(pattern="0RGBWCMY" width="4" height="2")");
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
    EXPECT_EQ(file.image(0).color_filter_array.value_or(color_filter_array{}).pattern, "0RGBWCMY");
}

TEST(conformance_cfa, the_pattern_width_and_height_are_mandatory)
{
    EXPECT_TRUE(unavailable(open_filter(R"(width="2" height="2")")));
    EXPECT_TRUE(unavailable(open_filter(R"(pattern="RGGB" height="2")")));
    EXPECT_TRUE(unavailable(open_filter(R"(pattern="RGGB" width="2")")));
}

TEST(conformance_cfa, the_pattern_has_one_element_of_table_18_for_each_pixel_of_the_matrix)
{
    EXPECT_TRUE(unavailable(open_filter(R"(pattern="RGGBR" width="2" height="2")")));
    EXPECT_TRUE(unavailable(open_filter(R"(pattern="RGB" width="2" height="2")")));
    EXPECT_TRUE(unavailable(open_filter(R"(pattern="rggb" width="2" height="2")")));
    EXPECT_TRUE(unavailable(open_filter(R"(pattern="RGXB" width="2" height="2")")));
    EXPECT_TRUE(unavailable(open_filter(R"(pattern="" width="0" height="0")")));
    EXPECT_TRUE(unavailable(open_filter(R"(pattern="RG" width="2" height="-1")")));
    EXPECT_TRUE(unavailable(open_filter(R"(pattern="RGGB" width="two" height="2")")));
}

TEST(conformance_cfa, a_colour_filter_array_belongs_to_a_two_dimensional_image)
{
    // Spec §11.10: the CFA of a two-dimensional image. That of a one-dimensional image is ignored.
    const reader file = openxisf::test::open_header(header_xml(
        R"(<Image geometry="4:1" sampleFormat="UInt8" location="embedded"><Data encoding="hex">01020304</Data>)"
        R"(<ColorFilterArray pattern="RGGB" width="2" height="2"/></Image>)"));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::invalid_color_filter_array,
                                  "/xisf/Image[1]/ColorFilterArray[1]"));
    EXPECT_FALSE(file.image(0).color_filter_array.has_value());
}

TEST(conformance_cfa, a_thumbnail_has_no_colour_filter_array)
{
    // Spec §11.12: thumbnails of mosaiced images are demosaiced; the element is not allowed there.
    const reader file = openxisf::test::open_header(header_xml(image_xml(
        {}, openxisf::test::thumbnail_xml({}, R"(<ColorFilterArray pattern="RGGB" width="2" height="2"/>)"))));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::unknown_element,
                                  "/xisf/Image[1]/Thumbnail[1]/ColorFilterArray[1]"));
    EXPECT_TRUE(file.image(0).thumbnail.has_value());
    EXPECT_FALSE(file.image(0).color_filter_array.has_value());
}

TEST(conformance_cfa, an_image_has_one_colour_filter_array)
{
    const reader file = openxisf::test::open_header(header_xml(image_xml(
        {}, R"(<ColorFilterArray pattern="RGGB" width="2" height="2"/><ColorFilterArray pattern="BGGR" width="2" )"
            R"(height="2"/>)")));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::duplicate_element,
                                  "/xisf/Image[1]/ColorFilterArray[2]"));
    EXPECT_EQ(file.image(0).color_filter_array.value_or(color_filter_array{}).pattern, "RGGB");
}

TEST(conformance_cfa, strict_reading_refuses_an_invalid_colour_filter_array)
{
    EXPECT_TRUE(openxisf::test::throws<openxisf::invalid_data_error>(errc::invalid_color_filter_array, [] {
        (void)open_filter(R"(pattern="RGB" width="2" height="2")", {.strict = true});
    }));
}

} // namespace
