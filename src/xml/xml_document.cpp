// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "xml/xml_document.h"

#include <openxisf/error.h>

#include "core/quote.h"
#include "core/utf8.h"

#include <algorithm>
#include <new>
#include <string>
#include <vector>

namespace openxisf::detail {

namespace {

// The default options of pugixml (CDATA sections, escapes, normalized white space in attribute values and line ends),
// plus the nodes that the checks need: the XML declaration, a document type declaration, and white space as the only
// content of an element.
constexpr unsigned int parse_options =
    pugi::parse_default | pugi::parse_ws_pcdata_single | pugi::parse_declaration | pugi::parse_doctype;

error_context context_of(const pugi::xml_node& element, std::uint64_t offset)
{
    if (const std::optional<std::size_t> start = element_offset(element)) {
        return {.offset = offset + *start};
    }
    return {};
}

// For a node that is not an element, pugixml gives the offset of its content.
error_context context_of_content(const pugi::xml_node& node, std::uint64_t offset)
{
    const std::ptrdiff_t content = node.offset_debug();
    if (content < 0) {
        return {};
    }
    return {.offset = offset + static_cast<std::uint64_t>(content)};
}

// pugixml accepts an attribute twice in an element, and finds the first. Another reader would refuse the element or
// take the last, so the meaning is not clear.
void check_unique_attributes(const pugi::xml_node& element, std::vector<std::string_view>& names, std::uint64_t offset)
{
    names.clear();
    for (const pugi::xml_attribute& attribute : element.attributes()) {
        names.emplace_back(attribute.name());
    }
    std::ranges::sort(names);
    if (const auto repeated = std::ranges::adjacent_find(names); repeated != names.end()) {
        throw invalid_data_error(errc::invalid_xml,
                                 "the element " + quote(element.name()) + " has the attribute " + quote(*repeated) +
                                     " more than once",
                                 context_of(element, offset));
    }
}

// The value of the character reference at text[start], which starts with "&#", as pugixml reads it: decimal digits, or
// x and hexadecimal digits, then a semicolon. Saturated beyond the last code point. Empty when pugixml leaves the
// reference as written.
std::optional<std::uint32_t> character_reference(std::string_view text, std::size_t start) noexcept
{
    constexpr std::uint32_t beyond_unicode = 0x110000;
    std::size_t i = start + 2;
    std::uint32_t radix = 10;
    if (i < text.size() && text[i] == 'x') {
        radix = 16;
        ++i;
    }
    std::uint32_t value = 0;
    std::size_t digits = 0;
    for (; i < text.size() && text[i] != ';'; ++i, ++digits) {
        const char c = text[i];
        std::uint32_t digit = 16;
        if (c >= '0' && c <= '9') {
            digit = static_cast<std::uint32_t>(c - '0');
        } else if (radix == 16 && c >= 'a' && c <= 'f') {
            digit = static_cast<std::uint32_t>(c - 'a') + 10;
        } else if (radix == 16 && c >= 'A' && c <= 'F') {
            digit = static_cast<std::uint32_t>(c - 'A') + 10;
        }
        if (digit >= radix) {
            return std::nullopt;
        }
        value = std::min((value * radix) + digit, beyond_unicode);
    }
    if (i == text.size() || digits == 0) {
        return std::nullopt;
    }
    return value;
}

// pugixml replaces a character reference with the UTF-8 form of any number, so a reference to U+0000, to a surrogate or
// beyond U+10FFFF would put a NUL that ends the text early, or invalid UTF-8, into a value. XML 1.0 forbids them (Legal
// Character). References in comments, CDATA sections and processing instructions are text.
void check_character_references(std::string_view text, std::uint64_t offset)
{
    const auto past = [text](std::size_t from, std::string_view terminator) {
        const std::size_t found = text.find(terminator, from);
        return found == std::string_view::npos ? text.size() : found + terminator.size();
    };
    std::size_t i = 0;
    while ((i = text.find_first_of("<&", i)) != std::string_view::npos) {
        const std::string_view rest = text.substr(i);
        if (rest.starts_with("<!--")) {
            i = past(i + 4, "-->");
        } else if (rest.starts_with("<![CDATA[")) {
            i = past(i + 9, "]]>");
        } else if (rest.starts_with("<?")) {
            i = past(i + 2, "?>");
        } else {
            if (rest.starts_with("&#")) {
                const std::optional<std::uint32_t> value = character_reference(text, i);
                if (value && (*value == 0 || (*value >= 0xD800 && *value <= 0xDFFF) || *value > 0x10FFFF)) {
                    throw invalid_data_error(errc::invalid_xml,
                                             "the character reference " + quote(rest.substr(0, rest.find(';') + 1)) +
                                                 " does not name a character that XML allows",
                                             {.offset = offset + i});
                }
            }
            ++i;
        }
    }
}

// Visits every node in document order, without recursion, so that no nesting depth can exhaust the stack.
void check_tree(const pugi::xml_document& document, std::uint64_t offset, const limits& limits)
{
    std::vector<std::string_view> names;
    std::uint64_t elements = 0;
    std::uint64_t depth = 1;
    pugi::xml_node node = document.first_child();
    while (!node.empty()) {
        if (node.type() == pugi::node_doctype) {
            throw invalid_data_error(errc::doctype_not_allowed,
                                     "the header has a document type declaration, which XISF does not allow",
                                     context_of_content(node, offset));
        }
        if (node.type() == pugi::node_element) {
            ++elements;
            if (limits.max_xml_elements != 0 && elements > limits.max_xml_elements) {
                throw limit_error(errc::too_many_xml_elements,
                                  "the header has more than " + std::to_string(limits.max_xml_elements) +
                                      " XML elements",
                                  context_of(node, offset));
            }
            if (limits.max_xml_depth != 0 && depth > limits.max_xml_depth) {
                throw limit_error(errc::xml_too_deep,
                                  "the header nests XML elements deeper than " + std::to_string(limits.max_xml_depth) +
                                      " levels",
                                  context_of(node, offset));
            }
            check_unique_attributes(node, names, offset);
        }

        // The next node: the first child, or else the next sibling of the node or of its closest ancestor that has
        // one.
        if (const pugi::xml_node child = node.first_child(); !child.empty()) {
            node = child;
            ++depth;
            continue;
        }
        while (!node.next_sibling() && node.parent().type() != pugi::node_document) {
            node = node.parent();
            --depth;
        }
        node = node.next_sibling();
    }
}

} // namespace

bool is_xml_white_space(std::string_view text) noexcept
{
    return std::ranges::all_of(text, [](char c) { return is_xml_white_space(c); });
}

std::unique_ptr<pugi::xml_document> parse_xml(std::string_view text, std::uint64_t offset, const limits& limits)
{
    if (!is_valid_utf8(text)) {
        throw invalid_data_error(errc::invalid_utf8, "the header is not valid UTF-8, or contains U+0000",
                                 {.offset = offset});
    }
    check_character_references(text, offset);

    auto document = std::make_unique<pugi::xml_document>();
    const pugi::xml_parse_result result =
        document->load_buffer(text.data(), text.size(), parse_options, pugi::encoding_utf8);
    if (result.status == pugi::status_out_of_memory) {
        throw std::bad_alloc();
    }
    if (!result) {
        throw invalid_data_error(errc::invalid_xml,
                                 std::string("the header is not well-formed XML: ") + result.description(),
                                 {.offset = offset + static_cast<std::uint64_t>(result.offset)});
    }
    check_tree(*document, offset, limits);
    return document;
}

std::string character_data(const pugi::xml_node& element)
{
    std::string text;
    for (pugi::xml_node child = element.first_child(); !child.empty(); child = child.next_sibling()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            text += child.value();
        }
    }
    return text;
}

std::optional<std::size_t> element_offset(const pugi::xml_node& element) noexcept
{
    // pugixml gives the offset of the name, which follows the '<' immediately.
    const std::ptrdiff_t name = element.offset_debug();
    if (name < 1) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(name - 1);
}

std::size_t element_length(std::string_view text, std::size_t start)
{
    // Past the next terminator from position from, or the end of the text.
    const auto past = [text](std::size_t from, std::string_view terminator) {
        const std::size_t found = text.find(terminator, from);
        return found == std::string_view::npos ? text.size() : found + terminator.size();
    };

    std::size_t depth = 0;
    std::size_t i = start;
    while ((i = text.find('<', i)) != std::string_view::npos) {
        // Markup that may hold '<' and '>' without being a tag.
        const std::string_view rest = text.substr(i);
        if (rest.starts_with("<!--")) {
            i = past(i + 4, "-->");
            continue;
        }
        if (rest.starts_with("<![CDATA[")) {
            i = past(i + 9, "]]>");
            continue;
        }
        if (rest.starts_with("<?")) {
            i = past(i + 2, "?>");
            continue;
        }

        // A tag ends at the first '>' outside its quoted attribute values.
        char quote_mark = 0;
        std::size_t end = i + 1;
        for (; end < text.size(); ++end) {
            const char c = text[end];
            if (quote_mark != 0) {
                if (c == quote_mark) {
                    quote_mark = 0;
                }
            } else if (c == '"' || c == '\'') {
                quote_mark = c;
            } else if (c == '>') {
                break;
            }
        }
        if (end == text.size()) {
            break;
        }
        i = end + 1;

        if (rest.starts_with("</")) {
            if (depth <= 1) {
                return i - start;
            }
            --depth;
        } else if (text[end - 1] == '/') {
            if (depth == 0) {
                return i - start;
            }
        } else if (!rest.starts_with("<!")) {
            // A start tag. No other declaration than those skipped above can be inside an element of a document that
            // parse_xml() accepted.
            ++depth;
        }
    }
    return text.size() - start;
}

} // namespace openxisf::detail
