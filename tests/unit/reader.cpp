// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include <openxisf/error.h>
#include <openxisf/io.h>
#include <openxisf/reader.h>

#include "support/bytes.h"
#include "support/diagnostics.h"
#include "support/faulty_io.h"
#include "support/files.h"
#include "support/fixture_builder.h"
#include "support/temp_directory.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using openxisf::errc;
using openxisf::reader;
using openxisf::severity;
using openxisf::unit_storage;
using openxisf::test::bytes;
using openxisf::test::header_xml;
using openxisf::test::monolithic_file;
using openxisf::test::no_diagnostics;
using openxisf::test::open_header;
using openxisf::test::temp_directory;
using openxisf::test::throws;

TEST(reader, opens_a_file_by_its_path)
{
    const temp_directory directory;
    const std::string path = directory.file("Nebulosa del Ñandú.xisf");
    openxisf::test::write_file(openxisf::test::path_of(path), monolithic_file(header_xml()));

    const reader file(path);

    EXPECT_EQ(file.storage(), unit_storage::monolithic);
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
}

TEST(reader, the_content_of_a_file_decides_what_it_holds_not_its_name)
{
    const temp_directory directory;
    const std::string path = directory.file("header.xisf");
    openxisf::test::write_file(openxisf::test::path_of(path), bytes(header_xml()));

    EXPECT_EQ(reader(path).storage(), unit_storage::distributed);
}

TEST(reader, refuses_a_null_source)
{
    EXPECT_TRUE(throws<openxisf::usage_error>(errc::invalid_argument,
                                              [] { reader file(std::unique_ptr<openxisf::input_source>{}); }));
}

TEST(reader, exceptions_of_the_source_pass_through)
{
    const openxisf::memory_source unit(monolithic_file(header_xml()));
    EXPECT_THROW((void)reader(std::make_unique<openxisf::test::faulty_source>(
                     unit, 2, openxisf::test::fault::foreign_exception)),
                 openxisf::test::injected_fault);
}

TEST(reader, strict_reading_fails_at_the_first_error_but_not_at_warnings)
{
    // A warning for the unknown element, then an error for the reference.
    const std::string header = header_xml(R"(<Unknown/><Reference ref="nothing"/><Reference ref="none"/>)");

    const reader lenient = open_header(header);
    ASSERT_EQ(lenient.diagnostics().size(), 3U);
    EXPECT_EQ(lenient.diagnostics()[0].severity, severity::warning);

    try {
        (void)open_header(header, {.strict = true});
        ADD_FAILURE();
    } catch (const openxisf::invalid_data_error& failure) {
        EXPECT_EQ(failure.code(), errc::dangling_reference);
        EXPECT_EQ(failure.context().element, "/xisf/Reference[1]");
    }

    EXPECT_EQ(open_header(header_xml("<Unknown/>"), {.strict = true}).diagnostics().size(), 1U);
}

TEST(reader, the_limits_of_the_options_apply)
{
    const std::string header = header_xml();
    EXPECT_TRUE(throws<openxisf::limit_error>(errc::header_too_large, [&header] {
        (void)open_header(header, {.limits = {.max_header_size = header.size() - 1}});
    }));
    EXPECT_TRUE(throws<openxisf::limit_error>(
        errc::too_many_xml_elements, [&header] { (void)open_header(header, {.limits = {.max_xml_elements = 3}}); }));
    EXPECT_TRUE(throws<openxisf::limit_error>(
        errc::xml_too_deep, [&header] { (void)open_header(header, {.limits = {.max_xml_depth = 2}}); }));
}

// -----------------------------------------------------------------------------------------------------------------
// Header-only opens

// A unit with ancillary data in data blocks, and without: an image with an inline ICC profile, an embedded thumbnail,
// a vector property in an attached block and a String property in its character data, a table with a cell in an
// inline block and one without, and a standalone property in an attached block whose checksum fails.
std::vector<std::byte> unit_with_ancillary_data()
{
    const std::string body =
        openxisf::test::image_xml(
            {}, R"(<ICCProfile location="inline:base64">YWNzcA==</ICCProfile>)" + openxisf::test::thumbnail_xml() +
                    R"(<Property id="Vector" type="UI8Vector" length="2" location="attachment:{0}"/>)"
                    R"(<Property id="Text" type="String">text</Property>)"
                    R"(<Table id="InBlock"><Structure><Field id="f" type="String"/></Structure>)"
                    R"(<Row><Cell location="inline:base64">YQ==</Cell></Row></Table>)"
                    R"(<Table id="Plain"><Structure><Field id="f" type="String"/></Structure>)"
                    R"(<Row><Cell>a</Cell></Row></Table>)") +
        R"(<Property id="Corrupt" type="UI8Vector" length="2" location="attachment:{1}" )"
        R"(checksum="sha1:0000000000000000000000000000000000000000"/>)";
    return openxisf::test::file_with_attachments(header_xml(body), {bytes("\x01\x02"), bytes("\x03\x04")});
}

TEST(reader, a_header_only_open_leaves_the_ancillary_data_in_data_blocks)
{
    const reader file = openxisf::test::open_unit(unit_with_ancillary_data(), {.header_only = true});
    EXPECT_FALSE(file.ancillary_data_loaded());
    // The data block of the corrupt property is not read, so nothing is found wrong with it.
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
    const openxisf::image_info& info = file.image(0);
    EXPECT_TRUE(info.icc_profile.empty());
    ASSERT_TRUE(info.thumbnail.has_value());
    EXPECT_EQ(info.thumbnail.value_or(openxisf::thumbnail{}).geometry.sample_count(), 1U);
    EXPECT_TRUE(info.thumbnail.value_or(openxisf::thumbnail{}).pixels.empty());
    EXPECT_FALSE(info.properties.contains("Vector"));
    EXPECT_TRUE(info.properties.contains("Text"));
    ASSERT_EQ(info.tables.size(), 1U);
    EXPECT_EQ(info.tables[0].id, "Plain");
    EXPECT_TRUE(file.properties().empty());
}

TEST(reader, loading_the_ancillary_data_gives_what_a_full_open_gives)
{
    reader file = openxisf::test::open_unit(unit_with_ancillary_data(), {.header_only = true});
    file.load_ancillary_data();
    EXPECT_TRUE(file.ancillary_data_loaded());
    const reader full = openxisf::test::open_unit(unit_with_ancillary_data());
    EXPECT_TRUE(full.ancillary_data_loaded());
    EXPECT_EQ(file.image(0), full.image(0));
    EXPECT_EQ(file.image(0).icc_profile, bytes("acsp"));
    EXPECT_EQ(file.image(0).tables.size(), 2U);
    EXPECT_EQ(file.properties(), full.properties());
    // The checksum of the corrupt property fails now, as it does in a full open.
    ASSERT_EQ(file.diagnostics().size(), 1U);
    EXPECT_EQ(file.diagnostics()[0].code, errc::checksum_mismatch);
    EXPECT_EQ(file.diagnostics()[0].context, full.diagnostics()[0].context);

    // A second call, or a call after a full open, does nothing.
    file.load_ancillary_data();
    EXPECT_EQ(file.image(0), full.image(0));
    EXPECT_EQ(file.diagnostics().size(), 1U);
}

TEST(reader, loading_the_ancillary_data_fails_like_an_open_and_leaves_the_reader_unchanged)
{
    reader file = openxisf::test::open_unit(unit_with_ancillary_data(), {.strict = true, .header_only = true});
    EXPECT_TRUE(throws<openxisf::integrity_error>(errc::checksum_mismatch, [&file] { file.load_ancillary_data(); }));
    EXPECT_FALSE(file.ancillary_data_loaded());
    EXPECT_TRUE(file.image(0).icc_profile.empty());
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
}

TEST(reader, moves)
{
    reader first = open_header(header_xml() + R"(<Signature xmlns="http://www.w3.org/2000/09/xmldsig#"/>)");
    reader second(std::move(first));
    EXPECT_EQ(second.signature(), openxisf::signature_status::not_verified);

    first = open_header(header_xml());
    second = std::move(first);
    EXPECT_EQ(second.signature(), openxisf::signature_status::none);
}

} // namespace
