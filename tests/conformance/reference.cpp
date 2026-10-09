// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Spec §11.13: Reference elements, on constructed units.

#include <openxisf/error.h>
#include <openxisf/image.h>
#include <openxisf/property.h>
#include <openxisf/reader.h>

#include "core/data_encoding.h"
#include "support/bytes.h"
#include "support/diagnostics.h"
#include "support/fixture_builder.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
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
    // The image holds a String property of 100 characters: a copy takes the image and the property themselves, and 125
    // bytes with the identifier of the property (9) and the two dimensions (16). The copies cannot multiply the memory
    // that a unit takes beyond the limit.
    constexpr std::uint64_t copy = sizeof(openxisf::image_info) + sizeof(openxisf::property) + 125;
    const std::string header = header_xml(image_xml(R"(uid="img")", R"(<Property id="Test:Text" type="String">)" +
                                                                        std::string(100, 'a') + "</Property>") +
                                          R"(<Reference ref="img"/><Reference ref="img"/>)");
    const reader fits = open_header(header, {.limits = {.max_ancillary_data = 2 * copy}});
    EXPECT_TRUE(no_diagnostics(fits.diagnostics()));
    EXPECT_EQ(fits.images().size(), 3U);
    const reader limited = open_header(header, {.limits = {.max_ancillary_data = (2 * copy) - 1}});
    EXPECT_TRUE(single_diagnostic(limited.diagnostics(), severity::error, errc::ancillary_data_too_large,
                                  "/xisf/Reference[2]"));
    ASSERT_EQ(limited.images().size(), 2U);
    EXPECT_TRUE(limited.image(1).properties.contains("Test:Text"));
}

// -----------------------------------------------------------------------------------------------------------------
// The elements that describe images, associated through References (spec §11.6 to §11.12)

// The standalone elements of each kind, with the uid "e", a check of what an image that names it has, and the size of
// its data block, which is loaded with the unit; another element of the kind, which gives an image something else; and
// whether an image can have many of the kind.
struct associated_element
{
    std::string_view kind{};
    std::string element{};
    bool (*holds)(const openxisf::image_info&) = nullptr;
    std::uint64_t block_size = 0;
    std::string other{};
    bool many = false;
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
         .holds = [](const openxisf::image_info& info) { return info.fits_keywords.size() == 1; },
         .other = R"(<FITSKeyword name="TELESCOP" value="'Scope'" comment="The telescope"/>)",
         .many = true},
        {.kind = "ICCProfile",
         .element = R"(<ICCProfile uid="e" location="inline:base64">)" +
                    openxisf::detail::encode_base64(openxisf::test::icc_profile()) + "</ICCProfile>",
         .holds = [](const openxisf::image_info& info) { return info.icc_profile.size() == 132; },
         .block_size = 132,
         .other = R"(<ICCProfile location="inline:base64">)" +
                  openxisf::detail::encode_base64(openxisf::test::icc_profile(140)) + "</ICCProfile>"},
        {.kind = "RGBWorkingSpace",
         .element = R"(<RGBWorkingSpace uid="e" gamma="1" x="0.648431:0.321152:0.155886" )"
                    R"(y="0.330856:0.597871:0.066044" Y="0.222491:0.716888:0.060621" name="Linear sRGB"/>)",
         .holds = [](const openxisf::image_info& info) { return info.rgb_working_space.has_value(); },
         .other = R"(<RGBWorkingSpace gamma="2.2" x="0.648431:0.230154:0.155886" y="0.330856:0.701572:0.066044" )"
                  R"(Y="0.311114:0.625662:0.063224"/>)"},
        {.kind = "DisplayFunction",
         .element = R"(<DisplayFunction uid="e" m="0.4:0.4:0.4:0.5" s="0:0:0:0" h="1:1:1:1" l="0:0:0:0" )"
                    R"(r="1:1:1:1" name="AutoStretch"/>)",
         .holds = [](const openxisf::image_info& info) { return info.display_function.has_value(); },
         .other = R"(<DisplayFunction m="0.5:0.5:0.5:0.5" s="0:0:0:0" h="1:1:1:1" l="0:0:0:0" r="1:1:1:1"/>)"},
        {.kind = "ColorFilterArray",
         .element = R"(<ColorFilterArray uid="e" pattern="RGGB" width="2" height="2" name="RGGB Bayer filter"/>)",
         .holds = [](const openxisf::image_info& info) { return info.color_filter_array.has_value(); },
         .other = R"(<ColorFilterArray pattern="BGGR" width="2" height="2"/>)"},
        {.kind = "Resolution",
         .element = R"(<Resolution uid="e" horizontal="300" vertical="300"/>)",
         .holds = [](const openxisf::image_info& info) { return info.resolution.has_value(); },
         .other = R"(<Resolution horizontal="200" vertical="200"/>)"},
        {.kind = "Thumbnail",
         .element = openxisf::test::thumbnail_xml(R"(uid="e")"),
         .holds = [](const openxisf::image_info& info) { return info.thumbnail.has_value(); },
         .block_size = 1,
         .other = openxisf::test::thumbnail_xml(R"(id="second")")},
        {.kind = "Table",
         .element = R"(<Table uid="e" id="T" caption="Observations"><Structure><Field id="f" type="String"/>)"
                    R"(</Structure></Table>)",
         .holds = [](const openxisf::image_info& info) { return info.tables.size() == 1; },
         .other = R"(<Table id="U"><Structure><Field id="g" type="UInt8"/></Structure></Table>)",
         .many = true},
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

TEST_P(conformance_reference_association, an_image_has_one_element_of_a_kind_but_keywords_and_tables)
{
    // Spec §11.6 to §11.12: an image has one element of each kind that describes it, but FITS keywords and tables, of
    // which it has any number. A second one, contained or named, is ignored with a warning, and the first in document
    // order is the element of the image, whether it is contained or named.
    const associated_element& tested = GetParam();
    const reader first = open_header(header_xml(image_xml({}, tested.element)));
    const reader contained = open_header(header_xml(image_xml({}, tested.element + tested.other)));
    const reader named =
        open_header(header_xml(tested.element + image_xml({}, R"(<Reference ref="e"/>)" + tested.other)));
    ASSERT_EQ(contained.images().size(), 1U);
    ASSERT_EQ(named.images().size(), 1U);
    if (tested.many) {
        EXPECT_TRUE(no_diagnostics(contained.diagnostics()));
        EXPECT_TRUE(no_diagnostics(named.diagnostics()));
        EXPECT_NE(contained.image(0), first.image(0));
    } else {
        const std::string path = "/xisf/Image[1]/" + std::string(tested.kind);
        EXPECT_TRUE(
            single_diagnostic(contained.diagnostics(), severity::warning, errc::duplicate_element, path + "[2]"));
        EXPECT_TRUE(single_diagnostic(named.diagnostics(), severity::warning, errc::duplicate_element, path + "[1]"));
        EXPECT_EQ(contained.image(0), first.image(0));
    }
    EXPECT_EQ(named.image(0), contained.image(0));
}

TEST_P(conformance_reference_association, the_copies_of_an_element_count_against_the_limit_of_loaded_data)
{
    // A limit of 5 bytes beyond the data block of the element, an ICC profile of 132 bytes or a thumbnail of 1 byte,
    // holds no copy, which takes at least the size of the object copied. The last image takes the element itself; a
    // table of the root element stays a standalone table, so both images take copies.
    const std::string named = image_xml({}, R"(<Reference ref="e"/>)");
    const reader file = open_header(header_xml(GetParam().element + named + named),
                                    {.limits = {.max_ancillary_data = GetParam().block_size + 5}});
    ASSERT_EQ(file.images().size(), 2U);
    if (GetParam().kind == "Table") {
        ASSERT_EQ(file.diagnostics().size(), 2U) << openxisf::test::describe(file.diagnostics());
        EXPECT_EQ(file.diagnostics()[1].context.element, "/xisf/Image[2]/Reference[1]");
        EXPECT_FALSE(GetParam().holds(file.image(1)));
        EXPECT_EQ(file.tables().size(), 1U);
        return;
    }
    EXPECT_TRUE(GetParam().holds(file.image(1)));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::error, errc::ancillary_data_too_large,
                                  "/xisf/Image[1]/Reference[1]"));
    EXPECT_FALSE(GetParam().holds(file.image(0)));
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

TEST(conformance_reference, an_element_ignored_as_a_second_one_costs_no_copy)
{
    // The first image ignores the profile it names, which it has already; the second image is then its last use, and
    // takes it without a copy. The limit holds the two profiles that are loaded, of 132 and 140 bytes, and no copy.
    const auto profile = [](std::string_view attributes, std::size_t size) {
        return "<ICCProfile " + std::string(attributes) + R"( location="inline:base64">)" +
               openxisf::detail::encode_base64(openxisf::test::icc_profile(size)) + "</ICCProfile>";
    };
    const reader file = open_header(header_xml(profile(R"(uid="i")", 132) +
                                               image_xml({}, profile({}, 140) + R"(<Reference ref="i"/>)") +
                                               image_xml({}, R"(<Reference ref="i"/>)")),
                                    {.limits = {.max_ancillary_data = 272}});
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::duplicate_element,
                                  "/xisf/Image[1]/Reference[1]"));
    ASSERT_EQ(file.images().size(), 2U);
    EXPECT_EQ(file.image(1).icc_profile.size(), 132U);
}

TEST(conformance_reference, a_table_refused_for_its_identifier_costs_no_copy)
{
    // The first image has a property with the identifier of the table that it names, and leaves the table out; the
    // second takes a copy, and the third, which contains the table, the table itself. The limit holds one copy: the
    // table with its identifier, and its field with its identifier.
    constexpr std::uint64_t copy = sizeof(openxisf::table) + sizeof(openxisf::table_field) + 2;
    const reader file =
        open_header(header_xml(image_xml({}, R"(<Property id="T" type="Int32" value="1"/><Reference ref="t"/>)") +
                               image_xml({}, R"(<Reference ref="t"/>)") +
                               image_xml({}, R"(<Table uid="t" id="T"><Structure><Field id="f" type="String"/>)"
                                             R"(</Structure></Table>)")),
                    {.limits = {.max_ancillary_data = copy}});
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::error, errc::duplicate_property_id,
                                  "/xisf/Image[1]/Reference[1]"));
    ASSERT_EQ(file.images().size(), 3U);
    EXPECT_TRUE(file.image(0).tables.empty());
    EXPECT_EQ(file.image(1).tables.size(), 1U);
    EXPECT_EQ(file.image(2).tables.size(), 1U);
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
