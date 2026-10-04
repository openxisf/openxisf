// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "model/header.h"

#include <openxisf/error.h>

#include "core/quote.h"
#include "xml/namespaces.h"
#include "xml/xml_document.h"

#include <algorithm>
#include <cstddef>
#include <unordered_map>

namespace openxisf::detail {

namespace {

constexpr std::string_view utf8_byte_order_mark = "\xEF\xBB\xBF";

// The XML declaration that every header starts with (spec §9.5).
constexpr std::string_view xml_declaration = R"(<?xml version="1.0" encoding="UTF-8"?>)";

char to_lower(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equal_ignoring_case(std::string_view a, std::string_view b) noexcept
{
    return std::ranges::equal(a, b, [](char x, char y) { return to_lower(x) == to_lower(y); });
}

bool is_ascii(std::string_view text) noexcept
{
    return std::ranges::all_of(text, [](char c) { return static_cast<unsigned char>(c) < 0x80U; });
}

// Another declaration, or none, means the same as long as the encoding is not in question: the header was found to be
// valid UTF-8, and pugixml reads it as such whatever the declaration says.
void check_declaration(const pugi::xml_document& document, std::string_view text, std::uint64_t offset,
                       diagnostic_log& log)
{
    if (text.starts_with(xml_declaration)) {
        return;
    }
    const pugi::xml_node declaration = document.first_child();
    if (declaration.type() != pugi::node_declaration) {
        log.warning(errc::invalid_xml_declaration, "the header does not start with an XML declaration",
                    {.offset = offset});
        return;
    }
    const std::string_view encoding = declaration.attribute("encoding").value();
    if (!encoding.empty() && !equal_ignoring_case(encoding, "UTF-8") && !is_ascii(text)) {
        throw invalid_data_error(errc::invalid_xml,
                                 "the XML declaration names the encoding " + quote(encoding) +
                                     ", but the header has characters beyond ASCII, which XISF encodes in UTF-8",
                                 {.offset = offset});
    }
    log.warning(errc::invalid_xml_declaration, "the XML declaration is not " + std::string(xml_declaration),
                {.offset = offset});
}

// The root element is named xisf, case-sensitively, in the XISF namespace or in none.
bool is_root_name(std::string_view name, const namespace_scope& scope) noexcept
{
    return local_name(name) == "xisf" && is_xisf_namespace(scope.namespace_of(name));
}

pugi::xml_node check_root(const pugi::xml_document& document, diagnostic_log& log)
{
    const pugi::xml_node root = document.document_element();
    const std::string path = "/" + std::string(root.name());
    namespace_scope scope;
    scope.enter(root);
    if (!is_root_name(root.name(), scope)) {
        throw invalid_data_error(errc::invalid_root_element,
                                 "the root element is " + quote(root.name()) + ", not an XISF root element",
                                 {.element = path});
    }

    const pugi::xml_attribute version = root.attribute("version");
    if (!version) {
        throw invalid_data_error(errc::invalid_root_element, "the root element has no version attribute",
                                 {.element = path, .attribute = "version"});
    }
    if (std::string_view(version.value()) != "1.0") {
        throw unsupported_error(errc::unsupported_version,
                                "the unit is of XISF version " + quote(version.value()) +
                                    ", and only version 1.0 is supported",
                                {.element = path, .attribute = "version"});
    }

    // White space between the child elements is not significant; other character data is not allowed.
    const auto is_text = [](const pugi::xml_node& child) {
        return (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) &&
               !is_xml_white_space(child.value());
    };
    if (!root.find_child(is_text).empty()) {
        log.warning(errc::invalid_root_element, "the root element has character data, which is ignored",
                    {.element = path});
    }
    return root;
}

// The signature exactly as written, which pugixml cannot reproduce: it would print the element in its own way.
std::string signature_text(const pugi::xml_node& signature, std::string_view text)
{
    const std::optional<std::size_t> start = element_offset(signature);
    if (!start) {
        // pugixml knows the offset of every element of a document parsed from one buffer.
        throw invalid_data_error(errc::invalid_xml, "the signature cannot be located in the header");
    }
    return std::string(text.substr(*start, element_length(text, *start)));
}

// A detached XML signature may follow the root element (spec §9.5). The first one that does is kept.
std::optional<std::string> check_after_root(const pugi::xml_node& root, std::string_view text, diagnostic_log& log)
{
    std::optional<std::string> signature;
    std::unordered_map<std::string_view, std::size_t> positions{{root.name(), 1}};
    for (pugi::xml_node node = root.next_sibling(); !node.empty(); node = node.next_sibling()) {
        if (node.type() != pugi::node_element) {
            continue;
        }
        const std::string_view name = node.name();
        const std::string path = "/" + std::string(name) + "[" + std::to_string(++positions[name]) + "]";
        namespace_scope scope;
        scope.enter(node);
        if (is_root_name(name, scope)) {
            throw invalid_data_error(errc::invalid_root_element, "the header has a second root element",
                                     {.element = path});
        }
        if (!signature && local_name(name) == "Signature" && scope.namespace_of(name) == xml_signature_namespace) {
            signature = signature_text(node, text);
            log.info(errc::signature_not_verified, "the unit is signed, and OpenXISF does not verify signatures",
                     {.element = path});
            continue;
        }
        log.warning(errc::unknown_element, "the element " + quote(name) + " after the root element is ignored",
                    {.element = path});
    }
    return signature;
}

} // namespace

parsed_header parse_header(std::string_view text, std::uint64_t offset, const limits& limits, diagnostic_log& log)
{
    if (text.starts_with(utf8_byte_order_mark)) {
        text.remove_prefix(utf8_byte_order_mark.size());
        offset += utf8_byte_order_mark.size();
    }
    parsed_header header{.document = parse_xml(text, offset, limits)};
    check_declaration(*header.document, text, offset, log);
    header.root = check_root(*header.document, log);
    header.signature = check_after_root(header.root, text, log);
    return header;
}

} // namespace openxisf::detail
