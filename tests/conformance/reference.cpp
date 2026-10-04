// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Spec §11.13: Reference elements, on constructed units.

#include <openxisf/error.h>
#include <openxisf/image.h>
#include <openxisf/reader.h>

#include "support/diagnostics.h"
#include "support/fixture_builder.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using openxisf::errc;
using openxisf::reader;
using openxisf::severity;
using openxisf::test::header_xml;
using openxisf::test::image_xml;
using openxisf::test::keyword_xml;
using openxisf::test::no_diagnostics;
using openxisf::test::open_header;
using openxisf::test::single_diagnostic;

TEST(conformance_reference, a_reference_names_a_core_element_by_its_uid)
{
    // As in the examples of spec §11.6 and §11.13: images share a keyword defined after them, and one image is listed
    // again at the top level.
    const reader file = open_header(header_xml(image_xml(R"(uid="foo_bar")", R"(<Reference ref="KWD001"/>)") +
                                               image_xml({}, R"(<Reference ref="KWD001"/>)") +
                                               keyword_xml(R"(uid="KWD001")") + R"(<Reference ref="foo_bar"/>)"));
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
}

TEST(conformance_reference, a_reference_without_ref_attribute_names_nothing)
{
    const reader file = open_header(header_xml(image_xml({}, "<Reference/>")));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::error, errc::dangling_reference,
                                  "/xisf/Image[1]/Reference[1]"));
}

TEST(conformance_reference, a_reference_to_no_element_is_dangling)
{
    // uids are case-sensitive.
    const reader file = open_header(header_xml(image_xml(R"(uid="Target")") + R"(<Reference ref="target"/>)"));
    ASSERT_TRUE(single_diagnostic(file.diagnostics(), severity::error, errc::dangling_reference, "/xisf/Reference[1]"));
    EXPECT_EQ(file.diagnostics().front().context.attribute, "ref");
}

TEST(conformance_reference, a_reference_cannot_name_a_part_of_a_core_element)
{
    // Field, Row, Cell and Data are not core elements, and a uid of theirs means nothing.
    const reader file = open_header(header_xml(R"(<Structure uid="s"><Field id="f" type="UInt8" uid="f"/></Structure>)"
                                               R"(<Reference ref="f"/>)"));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::error, errc::dangling_reference, "/xisf/Reference[1]"));
}

TEST(conformance_reference, a_reference_to_a_uid_of_several_elements_is_ambiguous)
{
    const reader file =
        open_header(header_xml(image_xml(R"(uid="a")") + image_xml(R"(uid="a")") + R"(<Reference ref="a"/>)"));
    const std::span<const openxisf::diagnostic> diagnostics = file.diagnostics();
    ASSERT_EQ(diagnostics.size(), 2U) << openxisf::test::describe(diagnostics);
    EXPECT_EQ(diagnostics[0].code, errc::duplicate_uid);
    EXPECT_EQ(diagnostics[0].context.element, "/xisf/Image[2]");
    EXPECT_EQ(diagnostics[1].severity, severity::error);
    EXPECT_EQ(diagnostics[1].code, errc::duplicate_uid);
    EXPECT_EQ(diagnostics[1].context.element, "/xisf/Reference[1]");
}

TEST(conformance_reference, a_reference_cannot_have_a_uid)
{
    const reader file = open_header(header_xml(image_xml(R"(uid="a")") + R"(<Reference uid="r" ref="a"/>)"));
    ASSERT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::invalid_uid, "/xisf/Reference[1]"));
    EXPECT_EQ(file.diagnostics().front().context.attribute, "uid");
}

TEST(conformance_reference, references_cannot_be_chained)
{
    const reader file = open_header(header_xml(image_xml(R"(uid="a")") + R"(<Reference uid="r" ref="a"/>)" +
                                               image_xml({}, R"(<Reference ref="r"/>)")));
    const std::span<const openxisf::diagnostic> diagnostics = file.diagnostics();
    ASSERT_EQ(diagnostics.size(), 2U) << openxisf::test::describe(diagnostics);
    EXPECT_EQ(diagnostics[0].code, errc::invalid_uid);
    EXPECT_EQ(diagnostics[1].severity, severity::error);
    EXPECT_EQ(diagnostics[1].code, errc::chained_reference);
    EXPECT_EQ(diagnostics[1].context.element, "/xisf/Image[2]/Reference[1]");
}

TEST(conformance_reference, strict_reading_fails_on_a_reference_that_names_nothing)
{
    const std::string header = header_xml(R"(<Reference ref="missing"/>)");
    EXPECT_TRUE(openxisf::test::throws<openxisf::invalid_data_error>(
        errc::dangling_reference, [&header] { (void)open_header(header, {.strict = true}); }));
}

TEST(conformance_reference, a_reference_of_the_root_element_lists_an_image_again)
{
    // Spec §11.13: the same image, where the Reference is, even before the Image element; its pixels are read again.
    const reader file = open_header(header_xml(R"(<Reference ref="img"/>)" + image_xml(R"(uid="img" id="first")") +
                                               image_xml(R"(id="second")") + R"(<Reference ref="img"/>)"));
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
    ASSERT_EQ(file.images().size(), 4U);
    EXPECT_EQ(file.image(0).id, "first");
    EXPECT_EQ(file.image(1).id, "first");
    EXPECT_EQ(file.image(2).id, "second");
    EXPECT_EQ(file.image(3), file.image(1));
    EXPECT_EQ(file.read_pixels(3), file.read_pixels(1));
}

TEST(conformance_reference, a_reference_of_the_root_element_to_anything_but_an_image_is_ignored)
{
    // An element of the root element is not associated with anything, so the Reference has no meaning.
    const reader file = open_header(header_xml(keyword_xml(R"(uid="k")") + R"(<Reference ref="k"/>)"));
    ASSERT_TRUE(
        single_diagnostic(file.diagnostics(), severity::warning, errc::invalid_reference, "/xisf/Reference[1]"));
    EXPECT_EQ(file.diagnostics().front().context.attribute, "ref");
    EXPECT_TRUE(file.images().empty());
}

TEST(conformance_reference, a_reference_of_the_root_element_to_a_thumbnail_lists_no_image)
{
    // A Thumbnail element is an Image element under another name, but not an image of the unit.
    const reader file =
        open_header(header_xml(openxisf::test::thumbnail_xml(R"(uid="t")") + R"(<Reference ref="t"/>)" + image_xml()));
    EXPECT_TRUE(
        single_diagnostic(file.diagnostics(), severity::warning, errc::invalid_reference, "/xisf/Reference[1]"));
    EXPECT_EQ(file.images().size(), 1U);
}

TEST(conformance_reference, an_image_is_not_associated_with_another_image)
{
    const reader file = open_header(header_xml(image_xml(R"(uid="a")") + image_xml({}, R"(<Reference ref="a"/>)")));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::invalid_reference,
                                  "/xisf/Image[2]/Reference[1]"));
    EXPECT_EQ(file.images().size(), 2U);
}

TEST(conformance_reference, the_copies_of_an_image_listed_again_count_against_the_limit_of_loaded_data)
{
    // The image holds a String property of 100 characters: a copy takes 125 bytes with the identifier of the property
    // (9) and the two dimensions (16). The copies cannot multiply the memory that a unit takes beyond the limit.
    const std::string header = header_xml(image_xml(R"(uid="img")", R"(<Property id="Test:Text" type="String">)" +
                                                                        std::string(100, 'a') + "</Property>") +
                                          R"(<Reference ref="img"/><Reference ref="img"/>)");
    const reader fits = open_header(header, {.limits = {.max_ancillary_data = 250}});
    EXPECT_TRUE(no_diagnostics(fits.diagnostics()));
    EXPECT_EQ(fits.images().size(), 3U);
    const reader limited = open_header(header, {.limits = {.max_ancillary_data = 249}});
    EXPECT_TRUE(single_diagnostic(limited.diagnostics(), severity::error, errc::ancillary_data_too_large,
                                  "/xisf/Reference[2]"));
    ASSERT_EQ(limited.images().size(), 2U);
    EXPECT_TRUE(limited.image(1).properties.contains("Test:Text"));
}

// -----------------------------------------------------------------------------------------------------------------
// The elements that describe images, associated through References (spec §11.6 to §11.12)

// The standalone elements of each kind, with the uid "e", and a check of what an image that names it has.
struct associated_element
{
    std::string_view kind{};
    std::string element{};
    bool (*holds)(const openxisf::image_info&) = nullptr;
};

// NOLINTNEXTLINE(readability-identifier-naming): GoogleTest finds the function by this name.
void PrintTo(const associated_element& element, std::ostream* output)
{
    *output << element.kind;
}

class conformance_reference_association : public testing::TestWithParam<associated_element>
{};

std::vector<associated_element> associated_elements()
{
    return {
        {.kind = "FITSKeyword",
         .element = keyword_xml(R"(uid="e")"),
         .holds = [](const openxisf::image_info& info) { return info.fits_keywords.size() == 1; }},
        {.kind = "ICCProfile",
         .element = R"(<ICCProfile uid="e" location="inline:base64">YWNzcA==</ICCProfile>)",
         .holds = [](const openxisf::image_info& info) { return info.icc_profile.size() == 4; }},
        {.kind = "RGBWorkingSpace",
         .element = R"(<RGBWorkingSpace uid="e" gamma="1" x="0.648431:0.321152:0.155886" )"
                    R"(y="0.330856:0.597871:0.066044" Y="0.222491:0.716888:0.060621" name="Linear sRGB"/>)",
         .holds = [](const openxisf::image_info& info) { return info.rgb_working_space.has_value(); }},
        {.kind = "DisplayFunction",
         .element = R"(<DisplayFunction uid="e" m="0.4:0.4:0.4:0.5" s="0:0:0:0" h="1:1:1:1" l="0:0:0:0" )"
                    R"(r="1:1:1:1" name="AutoStretch"/>)",
         .holds = [](const openxisf::image_info& info) { return info.display_function.has_value(); }},
        {.kind = "ColorFilterArray",
         .element = R"(<ColorFilterArray uid="e" pattern="RGGB" width="2" height="2" name="RGGB Bayer filter"/>)",
         .holds = [](const openxisf::image_info& info) { return info.color_filter_array.has_value(); }},
        {.kind = "Resolution",
         .element = R"(<Resolution uid="e" horizontal="300" vertical="300"/>)",
         .holds = [](const openxisf::image_info& info) { return info.resolution.has_value(); }},
        {.kind = "Thumbnail",
         .element = openxisf::test::thumbnail_xml(R"(uid="e")"),
         .holds = [](const openxisf::image_info& info) { return info.thumbnail.has_value(); }},
        {.kind = "Table",
         .element = R"(<Table uid="e" id="T" caption="Observations"><Structure><Field id="f" type="String"/>)"
                    R"(</Structure></Table>)",
         .holds = [](const openxisf::image_info& info) { return info.tables.size() == 1; }},
    };
}

TEST_P(conformance_reference_association, a_standalone_element_is_associated_with_each_image_that_names_it)
{
    // Spec §11.13: a Reference avoids repeating the element in each image; each image has it.
    const std::string named = image_xml({}, R"(<Reference ref="e"/>)");
    const reader file = open_header(header_xml(GetParam().element + named + named + image_xml()));
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
    ASSERT_EQ(file.images().size(), 3U);
    EXPECT_TRUE(GetParam().holds(file.image(0)));
    EXPECT_TRUE(GetParam().holds(file.image(1)));
    EXPECT_FALSE(GetParam().holds(file.image(2)));
    EXPECT_EQ(file.image(0), file.image(1));
}

TEST_P(conformance_reference_association, an_element_of_an_image_can_be_named_by_another_one)
{
    const reader file =
        open_header(header_xml(image_xml({}, GetParam().element) + image_xml({}, R"(<Reference ref="e"/>)")));
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
    ASSERT_EQ(file.images().size(), 2U);
    EXPECT_TRUE(GetParam().holds(file.image(0)));
    EXPECT_TRUE(GetParam().holds(file.image(1)));
}

TEST_P(conformance_reference_association, the_copies_of_an_element_count_against_the_limit_of_loaded_data)
{
    // A limit of 5 bytes holds the data blocks of the elements, an ICC profile of 4 bytes or a thumbnail of 1 byte,
    // but no copy, except that of a resolution, which holds nothing beyond its fixed size. The last image takes the
    // element itself; a table of the root element stays a standalone table, so both images take copies.
    const std::string named = image_xml({}, R"(<Reference ref="e"/>)");
    const reader file =
        open_header(header_xml(GetParam().element + named + named), {.limits = {.max_ancillary_data = 5}});
    ASSERT_EQ(file.images().size(), 2U);
    if (GetParam().kind == "Table") {
        ASSERT_EQ(file.diagnostics().size(), 2U) << openxisf::test::describe(file.diagnostics());
        EXPECT_EQ(file.diagnostics()[1].context.element, "/xisf/Image[2]/Reference[1]");
        EXPECT_FALSE(GetParam().holds(file.image(1)));
        EXPECT_EQ(file.tables().size(), 1U);
        return;
    }
    EXPECT_TRUE(GetParam().holds(file.image(1)));
    if (GetParam().kind == "Resolution") {
        EXPECT_TRUE(no_diagnostics(file.diagnostics()));
        EXPECT_TRUE(GetParam().holds(file.image(0)));
    } else {
        EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::error, errc::ancillary_data_too_large,
                                      "/xisf/Image[1]/Reference[1]"));
        EXPECT_FALSE(GetParam().holds(file.image(0)));
    }
}

INSTANTIATE_TEST_SUITE_P(elements, conformance_reference_association, testing::ValuesIn(associated_elements()),
                         [](const testing::TestParamInfo<associated_element>& parameter) {
                             return std::string(parameter.param.kind);
                         });

TEST(conformance_reference, an_image_is_associated_with_elements_that_describe_images_only)
{
    // The Metadata element and a Structure element describe no image.
    const reader file = open_header(header_xml(R"(<Structure uid="s"><Field id="f" type="String"/></Structure>)" +
                                               image_xml({}, R"(<Reference ref="s"/>)")));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::invalid_reference,
                                  "/xisf/Image[1]/Reference[1]"));
    ASSERT_EQ(file.images().size(), 1U);
    EXPECT_TRUE(file.image(0).tables.empty());
}

TEST(conformance_reference, a_thumbnail_is_not_associated_with_a_colour_filter_array)
{
    // Spec §11.12: the thumbnail of a mosaiced image is demosaiced.
    const reader file =
        open_header(header_xml(R"(<ColorFilterArray uid="c" pattern="RGGB" width="2" height="2"/>)" +
                               image_xml({}, openxisf::test::thumbnail_xml({}, R"(<Reference ref="c"/>)"))));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::invalid_reference,
                                  "/xisf/Image[1]/Thumbnail[1]/Reference[1]"));
    EXPECT_FALSE(file.image(0).color_filter_array.has_value());
}

TEST(conformance_reference, a_reference_and_an_element_of_the_same_kind_make_a_second_one)
{
    // The first in document order is the element of the image, whether it is contained or named.
    const reader file = open_header(header_xml(R"(<Resolution uid="r" horizontal="1" vertical="1"/>)" +
                                               image_xml({}, R"(<Reference ref="r"/><Resolution horizontal="2" )"
                                                             R"(vertical="2"/>)")));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::duplicate_element,
                                  "/xisf/Image[1]/Resolution[1]"));
    EXPECT_EQ(file.image(0).resolution, (openxisf::resolution{.horizontal = 1.0, .vertical = 1.0}));
}

TEST(conformance_reference, an_element_ignored_as_a_second_one_costs_no_copy)
{
    // The first image ignores the profile it names, which it has already; the second image is then its last use, and
    // takes it without a copy. The limit holds the two profiles that are loaded, of 4 bytes each, and no copy.
    const reader file = open_header(
        header_xml(R"(<ICCProfile uid="i" location="inline:base64">YWNzcA==</ICCProfile>)" +
                   image_xml({}, R"(<ICCProfile location="inline:base64">AAAAAA==</ICCProfile><Reference ref="i"/>)") +
                   image_xml({}, R"(<Reference ref="i"/>)")),
        {.limits = {.max_ancillary_data = 8}});
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::duplicate_element,
                                  "/xisf/Image[1]/Reference[1]"));
    ASSERT_EQ(file.images().size(), 2U);
    EXPECT_EQ(file.image(1).icc_profile.size(), 4U);
}

TEST(conformance_reference, a_reference_to_an_image_that_is_left_out_lists_nothing)
{
    // The image has no geometry; the error about it is the only diagnostic.
    const reader file = open_header(header_xml(R"(<Image uid="a" sampleFormat="UInt8" location="embedded">)"
                                               R"(<Data encoding="hex">00</Data></Image><Reference ref="a"/>)"));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::error, errc::invalid_image, "/xisf/Image[1]"));
    EXPECT_TRUE(file.images().empty());
}

} // namespace
