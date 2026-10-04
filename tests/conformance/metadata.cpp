// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Spec §11.4: the Metadata element, its mandatory properties, and the XISF namespace of its properties.

#include <openxisf/error.h>
#include <openxisf/property.h>
#include <openxisf/reader.h>

#include "support/diagnostics.h"
#include "support/fixture_builder.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

using openxisf::date_time;
using openxisf::errc;
using openxisf::property_type;
using openxisf::property_value;
using openxisf::reader;
using openxisf::severity;
using openxisf::test::header_xml;
using openxisf::test::no_diagnostics;
using openxisf::test::open_header;
using openxisf::test::single_diagnostic;

// A header whose root element holds content, without a Metadata element of the fixture builder.
std::string header_with(std::string_view content)
{
    return std::string(openxisf::test::xml_declaration) + std::string(openxisf::test::root_start_tag) +
           std::string(content) + "</xisf>";
}

std::string metadata_with(std::string_view properties)
{
    return "<Metadata>" + std::string(properties) + "</Metadata>";
}

constexpr std::string_view creation_time =
    R"(<Property id="XISF:CreationTime" type="TimePoint" value="2026-10-02T12:00:00Z"/>)";
constexpr std::string_view creator_application =
    R"(<Property id="XISF:CreatorApplication" type="String">OpenXISF tests 1.0</Property>)";

TEST(conformance_metadata, the_properties_of_the_metadata_element_are_those_of_the_unit)
{
    const reader file = open_header(header_xml({}, R"(<Property id="XISF:Title" type="String">M 31</Property>)"));
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
    ASSERT_EQ(file.metadata().size(), 3U);
    EXPECT_EQ(file.metadata().at("XISF:CreationTime").value,
              property_value(date_time{.year = 2026, .month = 10, .day = 2, .hour = 12}));
    EXPECT_EQ(file.metadata().at("XISF:CreatorApplication").value, property_value("OpenXISF tests 1.0"));
    EXPECT_EQ(file.metadata().at("XISF:Title").value, property_value("M 31"));
    EXPECT_TRUE(file.properties().empty());
}

TEST(conformance_metadata, a_unit_without_a_metadata_element_is_tolerated)
{
    const reader file = open_header(header_with(openxisf::test::image_xml()));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::invalid_metadata, "/xisf"));
    EXPECT_TRUE(file.metadata().empty());
}

TEST(conformance_metadata, the_mandatory_properties_may_be_missing)
{
    // Spec §11.4.1. Each one missing is a warning about the Metadata element.
    for (const std::string_view present : {creation_time, creator_application}) {
        const reader file = open_header(header_with(metadata_with(present)));
        EXPECT_TRUE(
            single_diagnostic(file.diagnostics(), severity::warning, errc::invalid_metadata, "/xisf/Metadata[1]"))
            << present;
        EXPECT_EQ(file.metadata().size(), 1U);
    }
    const reader empty = open_header(header_with(metadata_with({})));
    ASSERT_EQ(empty.diagnostics().size(), 2U);
    EXPECT_EQ(empty.diagnostics()[0].code, errc::invalid_metadata);
    EXPECT_EQ(empty.diagnostics()[1].code, errc::invalid_metadata);
    // An unavailable mandatory property is missing too.
    const reader broken = open_header(header_with(metadata_with(
        R"(<Property id="XISF:CreationTime" type="TimePoint" value="never"/>)" + std::string(creator_application))));
    ASSERT_EQ(broken.diagnostics().size(), 2U);
    EXPECT_EQ(broken.diagnostics()[0].code, errc::invalid_time_point);
    EXPECT_EQ(broken.diagnostics()[1].code, errc::invalid_metadata);
}

TEST(conformance_metadata, the_properties_of_several_metadata_elements_are_all_read)
{
    // Spec §11.4: there should be exactly one.
    const reader file = open_header(
        header_with(metadata_with(creation_time) + openxisf::test::image_xml() + metadata_with(creator_application)));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::invalid_metadata, "/xisf/Metadata[2]"));
    EXPECT_EQ(file.metadata().size(), 2U);
    // Their identifiers are those of one object.
    const reader repeated = open_header(header_with(
        metadata_with(std::string(creation_time) + std::string(creator_application)) + metadata_with(creation_time)));
    ASSERT_EQ(repeated.diagnostics().size(), 2U);
    EXPECT_EQ(repeated.diagnostics()[1].code, errc::duplicate_property_id);
    EXPECT_EQ(repeated.diagnostics()[1].context.element, "/xisf/Metadata[2]/Property[1]");
}

TEST(conformance_metadata, metadata_properties_are_in_the_xisf_namespace)
{
    // Spec §11.4. A property of another namespace is still read.
    const reader file = open_header(header_xml({}, R"(<Property id="Observer:Name" type="String">N</Property>)"));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::invalid_metadata,
                                  "/xisf/Metadata[1]/Property[3]"));
    EXPECT_TRUE(file.metadata().contains("Observer:Name"));
}

TEST(conformance_metadata, references_in_the_metadata_element_name_properties)
{
    // A standalone property associated with the unit, and a Reference to an image, which cannot be metadata.
    const reader file =
        open_header(header_xml(R"(<Property uid="title" id="XISF:Title" type="String">M 31</Property>)" +
                                   openxisf::test::image_xml(R"(uid="image")"),
                               R"(<Reference ref="title"/><Reference ref="image"/>)"));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::invalid_metadata,
                                  "/xisf/Metadata[1]/Reference[2]"));
    EXPECT_EQ(file.metadata().at("XISF:Title").value, property_value("M 31"));
    EXPECT_EQ(file.metadata().size(), 3U);
    EXPECT_TRUE(file.properties().contains("XISF:Title"));
}

TEST(conformance_metadata, a_creation_time_written_as_a_string_is_tolerated)
{
    // PixInsight 1.9.4 writes XISF:CreationTime as a String. Its files open, strictly too.
    const std::string header =
        header_with(metadata_with(R"(<Property id="XISF:CreationTime" type="String">2026-10-03T03:18:31Z</Property>)" +
                                  std::string(creator_application)));
    const reader file = open_header(header, {.strict = true});
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::reserved_property_type,
                                  "/xisf/Metadata[1]/Property[1]"));
    EXPECT_EQ(file.metadata().at("XISF:CreationTime").value.type(), property_type::string);
}

} // namespace
