// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Spec §11.11: Resolution elements, on constructed units.

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

using openxisf::errc;
using openxisf::reader;
using openxisf::resolution;
using openxisf::resolution_unit;
using openxisf::severity;
using openxisf::test::header_xml;
using openxisf::test::image_xml;
using openxisf::test::no_diagnostics;
using openxisf::test::single_diagnostic;

reader open_resolution(std::string_view attributes, openxisf::read_options options = {})
{
    return openxisf::test::open_header(header_xml(image_xml({}, "<Resolution " + std::string(attributes) + "/>")),
                                       std::move(options));
}

testing::AssertionResult unavailable(const reader& file)
{
    testing::AssertionResult found = single_diagnostic(file.diagnostics(), severity::error, errc::invalid_resolution,
                                                       "/xisf/Image[1]/Resolution[1]");
    if (found && file.image(0).resolution) {
        return testing::AssertionFailure() << "the image has a resolution";
    }
    return found;
}

TEST(conformance_resolution, the_example_of_the_specification_is_read)
{
    const reader file = open_resolution(R"(horizontal="120" vertical="120" unit="cm")");
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
    EXPECT_EQ(file.image(0).resolution,
              (resolution{.horizontal = 120.0, .vertical = 120.0, .unit = resolution_unit::centimeter}));
    EXPECT_EQ(resolution_unit_name(resolution_unit::centimeter), "cm");
    EXPECT_EQ(resolution_unit_name(resolution_unit::inch), "inch");
}

TEST(conformance_resolution, the_unit_is_inches_unless_it_says_otherwise)
{
    const reader file = open_resolution(R"(horizontal="300" vertical="150.5")");
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
    EXPECT_EQ(file.image(0).resolution, (resolution{.horizontal = 300.0, .vertical = 150.5}));
    EXPECT_EQ(open_resolution(R"(horizontal="1" vertical="1" unit="inch")").image(0).resolution,
              (resolution{.horizontal = 1.0, .vertical = 1.0}));
}

TEST(conformance_resolution, an_image_without_a_resolution_has_72_pixels_per_inch)
{
    const reader file = openxisf::test::open_header(header_xml(image_xml()));
    EXPECT_FALSE(file.image(0).resolution.has_value());
    EXPECT_EQ(resolution{}, (resolution{.horizontal = 72.0, .vertical = 72.0, .unit = resolution_unit::inch}));
}

TEST(conformance_resolution, both_values_are_mandatory_and_above_zero)
{
    EXPECT_TRUE(unavailable(open_resolution(R"(vertical="72")")));
    EXPECT_TRUE(unavailable(open_resolution(R"(horizontal="72")")));
    // The values refused are in unit/ancillary_attributes.cpp.
    EXPECT_TRUE(unavailable(open_resolution(R"(horizontal="72" vertical="0")")));
}

TEST(conformance_resolution, the_unit_is_inch_or_cm)
{
    // The units refused are in unit/ancillary_attributes.cpp.
    EXPECT_TRUE(unavailable(open_resolution(R"(horizontal="72" vertical="72" unit="mm")")));
}

TEST(conformance_resolution, strict_reading_refuses_an_invalid_resolution)
{
    EXPECT_TRUE(openxisf::test::throws<openxisf::invalid_data_error>(
        errc::invalid_resolution, [] { (void)open_resolution(R"(horizontal="0" vertical="72")", {.strict = true}); }));
}

} // namespace
