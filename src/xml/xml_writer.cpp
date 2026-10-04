// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "xml/xml_writer.h"

#include "core/utf8.h"

namespace openxisf::detail {

namespace {

// U+FFFE and U+FFFF in UTF-8.
constexpr std::string_view non_character_fffe = "\xEF\xBF\xBE";
constexpr std::string_view non_character_ffff = "\xEF\xBF\xBF";

void append_escaped(std::string& output, std::string_view text, bool attribute)
{
    for (const char c : text) {
        switch (c) {
        case '&':
            output += "&amp;";
            break;
        case '<':
            output += "&lt;";
            break;
        case '>':
            output += "&gt;";
            break;
        case '"':
            output += attribute ? "&quot;" : "\"";
            break;
        case '\t':
            output += attribute ? "&#9;" : "\t";
            break;
        case '\n':
            output += attribute ? "&#10;" : "\n";
            break;
        case '\r':
            output += "&#13;";
            break;
        default:
            output += c;
            break;
        }
    }
}

} // namespace

bool is_xml_text(std::string_view text) noexcept
{
    if (!is_valid_utf8(text)) {
        return false;
    }
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20U && c != '\t' && c != '\n' && c != '\r') {
            return false;
        }
    }
    return text.find(non_character_fffe) == std::string_view::npos &&
           text.find(non_character_ffff) == std::string_view::npos;
}

void append_attribute_value(std::string& output, std::string_view text)
{
    output += '"';
    append_escaped(output, text, true);
    output += '"';
}

void append_character_data(std::string& output, std::string_view text)
{
    append_escaped(output, text, false);
}

} // namespace openxisf::detail
