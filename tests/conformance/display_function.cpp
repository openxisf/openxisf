// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Spec §8.5.6 and §11.9: DisplayFunction elements, on constructed units.

#include <openxisf/error.h>
#include <openxisf/image.h>
#include <openxisf/reader.h>

#include "support/diagnostics.h"
#include "support/fixture_builder.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace {

using openxisf::display_function;
using openxisf::errc;
using openxisf::reader;
using openxisf::severity;
using openxisf::test::header_xml;
using openxisf::test::image_xml;
using openxisf::test::no_diagnostics;
using openxisf::test::single_diagnostic;

// The parameters of the example of spec §11.9.2.
constexpr std::string_view example = R"(m="0.000735:0.000735:0.000735:0.5" s="0.003758:0.003758:0.003758:0" )"
                                     R"(h="1:1:1:1" l="0:0:0:0" r="1:1:1:1")";

reader open_function(std::string_view attributes, openxisf::read_options options = {})
{
    return openxisf::test::open_header(header_xml(image_xml({}, "<DisplayFunction " + std::string(attributes) + "/>")),
                                       std::move(options));
}

testing::AssertionResult unavailable(const reader& file)
{
    testing::AssertionResult found = single_diagnostic(
        file.diagnostics(), severity::error, errc::invalid_display_function, "/xisf/Image[1]/DisplayFunction[1]");
    if (found && file.image(0).display_function) {
        return testing::AssertionFailure() << "the image has a display function";
    }
    return found;
}

TEST(conformance_display_function, the_example_of_the_specification_is_read)
{
    const reader file = open_function(std::string(example) + R"( name="AutoStretch")");
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
    EXPECT_EQ(file.image(0).display_function, (display_function{.midtones = {0.000735, 0.000735, 0.000735, 0.5},
                                                                .shadows = {0.003758, 0.003758, 0.003758, 0.0},
                                                                .name = "AutoStretch"}));
}

TEST(conformance_display_function, an_image_without_a_display_function_has_the_identity)
{
    // Spec §8.5.6, equation [9]: m = 1/2, s = 0, h = 1, l = 0 and r = 1.
    const reader file = openxisf::test::open_header(header_xml(image_xml()));
    EXPECT_FALSE(file.image(0).display_function.has_value());
    const display_function identity;
    EXPECT_EQ(identity.midtones, (std::array<double, 4>{0.5, 0.5, 0.5, 0.5}));
    EXPECT_EQ(identity.shadows, (std::array<double, 4>{0.0, 0.0, 0.0, 0.0}));
    EXPECT_EQ(identity.highlights, (std::array<double, 4>{1.0, 1.0, 1.0, 1.0}));
    EXPECT_EQ(identity.shadows_expansion, (std::array<double, 4>{0.0, 0.0, 0.0, 0.0}));
    EXPECT_EQ(identity.highlights_expansion, (std::array<double, 4>{1.0, 1.0, 1.0, 1.0}));
}

TEST(conformance_display_function, every_parameter_is_mandatory)
{
    for (const std::string_view missing : {"m", "s", "h", "l", "r"}) {
        std::string attributes;
        for (const std::string_view name : {"m", "s", "h", "l", "r"}) {
            if (name != missing) {
                attributes += std::string(name) + R"(="0.5:0.5:0.5:0.5" )";
            }
        }
        EXPECT_TRUE(unavailable(open_function(attributes))) << missing;
    }
}

TEST(conformance_display_function, each_parameter_has_four_finite_values)
{
    // The lists refused are in unit/ancillary_attributes.cpp.
    EXPECT_TRUE(unavailable(open_function(R"(m="0.5:0.5:0.5" s="0:0:0:0" h="1:1:1:1" l="0:0:0:0" r="1:1:1:1")")));
}

TEST(conformance_display_function, the_parameters_are_within_their_constraints)
{
    // Spec §8.5.6: 0 <= m, s, h <= 1, s <= h, l <= 0 and r >= 1, for each component; each constraint is tested in
    // unit/ancillary_attributes.cpp.
    EXPECT_TRUE(
        unavailable(open_function(R"(m="0.5:0.5:0.5:0.5" s="0:0.6:0:0" h="1:0.5:1:1" l="0:0:0:0" r="1:1:1:1")")));
    // The limits themselves are within them, each in the parameter of its attribute.
    const reader limits = open_function(R"(m="0:1:0.5:0.5" s="0.5:0:1:0" h="0.5:0:1:1" l="-2:0:0:0" r="3:1:1:1")");
    EXPECT_TRUE(no_diagnostics(limits.diagnostics()));
}

TEST(conformance_display_function, strict_reading_refuses_an_invalid_display_function)
{
    EXPECT_TRUE(openxisf::test::throws<openxisf::invalid_data_error>(errc::invalid_display_function, [] {
        (void)open_function(R"(m="2:0.5:0.5:0.5" s="0:0:0:0" h="1:1:1:1" l="0:0:0:0" r="1:1:1:1")", {.strict = true});
    }));
}

} // namespace
