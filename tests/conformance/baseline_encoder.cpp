// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Spec §7.1: the abilities of a baseline encoder, each checked in what the writer writes with its default options.

#include <openxisf/image.h>
#include <openxisf/property.h>
#include <openxisf/reader.h>
#include <openxisf/writer.h>

#include "support/bytes.h"
#include "support/diagnostics.h"
#include "support/fixture_builder.h"
#include "support/written_unit.h"

#include <gtest/gtest.h>
#include <pugixml.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using openxisf::property_type;

openxisf::write_options options()
{
    return {.creator_application = "OpenXISF tests 1.0",
            .creation_time = openxisf::date_time{.year = 2026, .month = 10, .day = 4}};
}

// A monolithic file with a single image, its pixel data attached, planar, in UInt16 samples, and the mandatory
// metadata.
void check_single_image(openxisf::color_space space)
{
    openxisf::image_info image;
    image.geometry = {.dimensions = {5, 3}, .channels = openxisf::nominal_channels(space)};
    image.color_space = space;
    std::vector<std::uint16_t> samples(image.geometry.sample_count());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<std::uint16_t>(i * 1000);
    }
    openxisf::writer output(options());
    (void)output.add_image(image, std::span<const std::uint16_t>(samples));
    const std::vector<std::byte> file = openxisf::test::written(output);

    EXPECT_EQ(openxisf::test::text(std::span(file).first(8)), "XISF0100");
    const auto header = openxisf::test::parsed_header(file);
    const pugi::xml_node root = header->document_element();
    EXPECT_STREQ(root.name(), "xisf");
    EXPECT_STREQ(root.attribute("version").value(), "1.0");
    EXPECT_STREQ(root.attribute("xmlns").value(), "http://www.pixinsight.com/xisf");

    const pugi::xml_node element = root.child("Image");
    ASSERT_FALSE(element.empty());
    EXPECT_TRUE(element.next_sibling("Image").empty());
    EXPECT_TRUE(std::string_view(element.attribute("location").value()).starts_with("attachment:"));
    EXPECT_STREQ(element.attribute("sampleFormat").value(), "UInt16");
    EXPECT_STREQ(element.attribute("colorSpace").value(), space == openxisf::color_space::gray ? "Gray" : "RGB");
    // Planar, the default storage model, which needs no attribute.
    EXPECT_TRUE(element.attribute("pixelStorage").empty());

    const openxisf::reader unit = openxisf::test::open_unit(file, {.strict = true});
    EXPECT_TRUE(openxisf::test::no_diagnostics(unit.diagnostics()));
    EXPECT_EQ(unit.metadata().at("XISF:CreationTime").value.type(), property_type::time_point);
    EXPECT_EQ(unit.metadata().at("XISF:CreatorApplication").value.type(), property_type::string);
    EXPECT_EQ(unit.read_pixels<std::uint16_t>(0), samples);
}

TEST(conformance_baseline_encoder, writes_a_single_gray_image_as_a_baseline_decoder_reads_it)
{
    check_single_image(openxisf::color_space::gray);
}

TEST(conformance_baseline_encoder, writes_a_single_rgb_image_as_a_baseline_decoder_reads_it)
{
    check_single_image(openxisf::color_space::rgb);
}

TEST(conformance_baseline_encoder, writes_properties_of_every_scalar_type_of_8_to_64_bits)
{
    openxisf::writer output(options());
    openxisf::property_list& properties = output.properties();
    properties.set("Test:Boolean", false);
    properties.set("Test:Int8", std::int8_t{-1});
    properties.set("Test:UInt8", std::uint8_t{2});
    properties.set("Test:Int16", std::int16_t{-3});
    properties.set("Test:UInt16", std::uint16_t{4});
    properties.set("Test:Int32", std::int32_t{-5});
    properties.set("Test:UInt32", std::uint32_t{6});
    properties.set("Test:Int64", std::int64_t{-7});
    properties.set("Test:UInt64", std::uint64_t{8});
    properties.set("Test:Float32", 9.5F);
    properties.set("Test:Float64", -10.25);

    const openxisf::reader unit = openxisf::test::open_unit(openxisf::test::written(output), {.strict = true});
    EXPECT_EQ(unit.properties(), output.properties());
    const auto header = openxisf::test::parsed_header(openxisf::test::written(output));
    for (pugi::xml_node property = header->document_element().child("Property"); !property.empty();
         property = property.next_sibling("Property")) {
        // As plain-text values (spec §11.1.4).
        EXPECT_FALSE(property.attribute("value").empty()) << property.attribute("id").value();
    }
}

} // namespace
