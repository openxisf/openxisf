// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "xml/xml_document.h"

#include <openxisf/error.h>
#include <openxisf/limits.h>

#include "support/throws.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace {

using namespace std::string_view_literals;
using openxisf::errc;
using openxisf::invalid_data_error;
using openxisf::limit_error;
using openxisf::detail::element_length;
using openxisf::detail::is_xml_white_space;
using openxisf::detail::parse_xml;
using openxisf::test::throws;

std::unique_ptr<pugi::xml_document> parse(std::string_view text, const openxisf::limits& limits = {})
{
    return parse_xml(text, 0, limits);
}

testing::AssertionResult refuses(errc code, std::string_view text)
{
    return throws<invalid_data_error>(code, [text] { (void)parse(text); });
}

// count elements nested in one another.
std::string nested(std::size_t count)
{
    std::string text;
    for (std::size_t i = 0; i < count; ++i) {
        text += "<e>";
    }
    for (std::size_t i = 0; i < count; ++i) {
        text += "</e>";
    }
    return text;
}

TEST(xml_document, accepts_several_elements_at_the_top_level)
{
    // As in a signed header, where the signature follows the root element.
    const auto document = parse("<a><b/></a>\n<c/>");

    EXPECT_STREQ(document->document_element().name(), "a");
    EXPECT_STREQ(document->document_element().next_sibling().name(), "c");
}

TEST(xml_document, refuses_malformed_xml)
{
    for (const std::string_view text :
         {""sv, "text"sv, "<a>"sv, "<a></b>"sv, "<a b=c/>"sv, "<a/><"sv, "<a><!-- </a>"sv}) {
        EXPECT_TRUE(refuses(errc::invalid_xml, text)) << text;
    }
}

TEST(xml_document, places_an_error_in_the_source)
{
    for (const std::uint64_t offset : {0U, 100U}) {
        try {
            (void)parse_xml("<a><b></a>", offset, {});
            ADD_FAILURE();
        } catch (const invalid_data_error& failure) {
            ASSERT_TRUE(failure.context().offset.has_value());
            EXPECT_GE(*failure.context().offset, offset + 6);
            EXPECT_LE(*failure.context().offset, offset + 10);
        }
    }
}

TEST(xml_document, refuses_text_that_is_not_utf8)
{
    for (const std::string_view text : {"<a>\xC0\x80</a>"sv, "<a>\xFF</a>"sv, "<a>\xED\xA0\x80</a>"sv, "<a>\0</a>"sv}) {
        EXPECT_TRUE(refuses(errc::invalid_utf8, text)) << text;
    }
}

TEST(xml_document, refuses_a_document_type_declaration)
{
    // The entities of a document type declaration are the way to external resources and to exponential expansion.
    for (const std::string_view text : {
             R"(<!DOCTYPE xisf><xisf/>)"sv,
             R"(<!DOCTYPE xisf SYSTEM "http://example.com/xisf.dtd"><xisf/>)"sv,
             R"(<!DOCTYPE xisf [<!ENTITY a "aaaa"><!ENTITY b "&a;&a;&a;&a;">]><xisf>&b;</xisf>)"sv,
         }) {
        EXPECT_TRUE(refuses(errc::doctype_not_allowed, text)) << text;
    }
}

TEST(xml_document, replaces_only_predefined_entities_and_character_references)
{
    const auto document = parse(R"(<a b="&lt;&#x41;&gt;">&amp;&#66;&quot;&apos;&undeclared;</a>)");
    const pugi::xml_node element = document->document_element();

    EXPECT_STREQ(element.attribute("b").value(), "<A>");
    EXPECT_STREQ(element.child_value(), "&B\"'&undeclared;");
}

TEST(xml_document, refuses_character_references_to_what_xml_does_not_allow)
{
    // XML 1.0, Legal Character: U+0000, surrogates and numbers beyond U+10FFFF, which pugixml would write into the
    // text.
    for (const std::string_view text :
         {"<a>&#0;</a>", "<a>&#x0;</a>", "<a>x&#00000;y</a>", R"(<a b="&#0;"/>)", "<a>&#xD800;</a>", "<a>&#xdfff;</a>",
          "<a>&#55296;</a>", "<a>&#x110000;</a>", "<a>&#1114112;</a>", "<a>&#4294967296;</a>", "<a>&#x100000000;</a>",
          "<a>&#99999999999999999999;</a>"}) {
        EXPECT_TRUE(refuses(errc::invalid_xml, text)) << text;
    }
    // The last code point and those around the surrogates are characters; references in comments, CDATA sections and
    // processing instructions are text; and what pugixml does not read as a reference stays as written.
    for (const std::string_view text :
         {"<a>&#x10FFFF;&#xD7FF;&#xE000;&#1;&#x9;</a>", "<a><!-- &#0; --></a>", "<a><![CDATA[&#0;]]></a>",
          "<a><?pi &#0;?></a>", "<a>&#X0;</a>", "<a>&#;</a>", "<a>&#x;</a>", "<a>&#0</a>", "<a>&#0x1;</a>"}) {
        EXPECT_NO_THROW((void)parse(text)) << text;
    }
    EXPECT_STREQ(parse("<a>&#x10FFFF;</a>")->document_element().child_value(), "\xF4\x8F\xBF\xBF");
}

TEST(xml_document, refuses_an_element_that_repeats_an_attribute)
{
    EXPECT_TRUE(refuses(errc::invalid_xml, R"(<a x="1" x="2"/>)"));
    try {
        (void)parse_xml(R"(<a><b y="1" x="2" y="1"/></a>)", 100, {});
        ADD_FAILURE();
    } catch (const invalid_data_error& failure) {
        EXPECT_EQ(failure.code(), errc::invalid_xml);
        EXPECT_EQ(failure.context().offset, 103U);
    }
}

TEST(xml_document, attributes_with_different_prefixes_are_different)
{
    EXPECT_NO_THROW((void)parse(R"(<a xmlns:p="urn:p" xmlns:q="urn:q" x="1" p:x="2" q:x="3"/>)"));
}

TEST(xml_document, keeps_white_space_that_is_the_only_content_of_an_element)
{
    // The value of a String property (spec §11.1.6).
    const auto document = parse("<a><b>  </b> <c> x </c>\n</a>");
    const pugi::xml_node a = document->document_element();

    EXPECT_STREQ(a.child("b").child_value(), "  ");
    EXPECT_STREQ(a.child("c").child_value(), " x ");
    // White space between elements is dropped.
    EXPECT_EQ(a.first_child().type(), pugi::node_element);
    EXPECT_EQ(a.last_child().type(), pugi::node_element);
}

TEST(xml_document, normalizes_line_ends_and_white_space_in_attributes)
{
    const auto document = parse("<a b=\"1\t2\n3\">x\r\ny</a>");
    EXPECT_STREQ(document->document_element().attribute("b").value(), "1 2 3");
    EXPECT_STREQ(document->document_element().child_value(), "x\ny");
}

TEST(xml_document, limits_the_nesting_depth)
{
    EXPECT_NO_THROW((void)parse(nested(5), {.max_xml_depth = 5}));
    EXPECT_TRUE(throws<limit_error>(errc::xml_too_deep, [] { (void)parse(nested(6), {.max_xml_depth = 5}); }));

    // The depth goes down again after each element.
    const std::string_view siblings = "<a><b><c/></b><d><e/></d></a><f><g/></f>";
    EXPECT_NO_THROW((void)parse(siblings, {.max_xml_depth = 3}));
    EXPECT_TRUE(throws<limit_error>(errc::xml_too_deep, [siblings] { (void)parse(siblings, {.max_xml_depth = 2}); }));
}

TEST(xml_document, limits_the_number_of_elements)
{
    // Every element counts, also those after the first top-level one.
    EXPECT_NO_THROW((void)parse("<a><b/><c/></a>", {.max_xml_elements = 3}));
    EXPECT_TRUE(throws<limit_error>(errc::too_many_xml_elements,
                                    [] { (void)parse("<a><b/><c/></a><d/>", {.max_xml_elements = 3}); }));
}

TEST(xml_document, zero_means_no_limit)
{
    // Deep enough to exhaust the stack of any recursive walk.
    EXPECT_NO_THROW((void)parse(nested(100'000), {.max_xml_depth = 0, .max_xml_elements = 0}));
}

TEST(xml_document, recognizes_xml_white_space)
{
    EXPECT_TRUE(is_xml_white_space(" \t\r\n"));
    EXPECT_TRUE(is_xml_white_space(""));
    for (const std::string_view text : {"x"sv, "\v"sv, "\f"sv, "\xC2\xA0"sv}) {
        EXPECT_FALSE(is_xml_white_space(text)) << text;
    }
}

TEST(xml_document, finds_the_end_of_an_element)
{
    struct example
    {
        std::string_view text{};
        std::size_t start = 0;
        std::string_view element{};
    };
    for (const example& e : {
             example{.text = "<a/>tail", .start = 0, .element = "<a/>"},
             example{.text = "<a></a>tail", .start = 0, .element = "<a></a>"},
             example{.text = "<x/><a b='1'><a/><a>t</a></a>tail", .start = 4, .element = "<a b='1'><a/><a>t</a></a>"},
             // Each of these holds what would end the element if it were read as tags.
             example{.text = R"(<a b="/>" c='>'></a>tail)", .start = 0, .element = R"(<a b="/>" c='>'></a>)"},
             example{.text = "<a><!-- > </a> --></a>tail", .start = 0, .element = "<a><!-- > </a> --></a>"},
             example{.text = "<a><![CDATA[ > </a> ]]></a>tail", .start = 0, .element = "<a><![CDATA[ > </a> ]]></a>"},
             example{.text = "<a><?p /> </a>?></a>tail", .start = 0, .element = "<a><?p /> </a>?></a>"},
             example{.text = "<a>no end", .start = 0, .element = "<a>no end"},
         }) {
        EXPECT_EQ(e.text.substr(e.start, element_length(e.text, e.start)), e.element);
    }
}

} // namespace
