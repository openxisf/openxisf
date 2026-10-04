// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// XML text as the encoder writes it, read back by the parser of the decoder.

#include "xml/xml_writer.h"

#include "xml/xml_document.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

using namespace std::string_view_literals;
using openxisf::detail::append_attribute_value;
using openxisf::detail::append_character_data;
using openxisf::detail::is_xml_text;

// Text that needs every escape: markup, quotes, the end of a CDATA section, white space of every kind, and a line
// ending of each convention, which a parser would otherwise normalize.
constexpr std::string_view difficult = "a<b>&c \"d\" 'e' ]]> \t1\n2\r3\r\n4 \xC3\x91"
                                       "and\xC3\xBA \xE2\x9C\x93";

TEST(xml_writer, accepts_the_characters_of_xml)
{
    EXPECT_TRUE(is_xml_text(""));
    EXPECT_TRUE(is_xml_text(difficult));
    // U+007F to U+009F are discouraged but allowed, and so are noncharacters other than U+FFFE and U+FFFF.
    EXPECT_TRUE(is_xml_text("\x7F\xC2\x80\xC2\x9F\xEF\xB7\x90\xF4\x8F\xBF\xBD"));
}

TEST(xml_writer, refuses_what_xml_cannot_hold)
{
    for (const std::string_view text : {"a\0b"sv, "\x01"sv, "\x08"sv, "\x0B"sv, "\x0C"sv, "\x1F"sv, "\xEF\xBF\xBE"sv,
                                        "\xEF\xBF\xBF"sv, "\xED\xA0\x80"sv, "\xC3"sv, "\xFF"sv}) {
        EXPECT_FALSE(is_xml_text(text)) << testing::PrintToString(std::string(text));
    }
}

TEST(xml_writer, attribute_values_read_back_unchanged)
{
    std::string document = "<e a=";
    append_attribute_value(document, difficult);
    document += "/>";
    EXPECT_EQ(document.find('\t'), std::string::npos);
    EXPECT_EQ(document.find('\n'), std::string::npos);
    EXPECT_EQ(document.find('\r'), std::string::npos);

    const auto parsed = openxisf::detail::parse_xml(document, 0, {});
    EXPECT_EQ(std::string_view(parsed->document_element().attribute("a").value()), difficult);
}

TEST(xml_writer, character_data_read_back_unchanged)
{
    std::string document = "<e>";
    append_character_data(document, difficult);
    document += "</e>";
    EXPECT_EQ(document.find('\r'), std::string::npos);

    const auto parsed = openxisf::detail::parse_xml(document, 0, {});
    EXPECT_EQ(openxisf::detail::character_data(parsed->document_element()), difficult);
}

TEST(xml_writer, white_space_alone_reads_back_as_character_data)
{
    for (const std::string_view text : {" "sv, "  \t "sv, "\n"sv, "\r\n"sv}) {
        std::string document = "<e>";
        append_character_data(document, text);
        document += "</e>";
        const auto parsed = openxisf::detail::parse_xml(document, 0, {});
        EXPECT_EQ(openxisf::detail::character_data(parsed->document_element()), text)
            << testing::PrintToString(std::string(text));
    }
}

} // namespace
