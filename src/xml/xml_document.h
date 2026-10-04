// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/limits.h>

#include <pugixml.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// XML 1.0 from untrusted input, through pugixml. pugixml does not check every well-formedness rule; what it leaves out
// and matters to XISF is checked here.

namespace openxisf::detail {

/// True for the white space of XML 1.0: space, tab, carriage return and line feed.
[[nodiscard]] constexpr bool is_xml_white_space(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/// True when text is empty or holds only XML white space.
[[nodiscard]] bool is_xml_white_space(std::string_view text) noexcept;

/// Parses text, which is untrusted, as an XML document. offset is where the text starts in its source, for the context
/// of errors.
///
/// The document may have several elements at its top level, as a signed XISF header has (spec §9.5). Character data
/// that is only white space is kept when it is the only content of its element, where it can be the value of a String
/// property (spec §11.1.6), and dropped elsewhere. The predefined entities and character references are replaced; any
/// other entity reference is left as written, since entities cannot be declared.
///
/// Throws invalid_data_error when the text is not valid UTF-8 (errc::invalid_utf8), is not well-formed XML, refers to
/// a character that XML does not allow, such as U+0000, or repeats an attribute of an element (errc::invalid_xml), or
/// has a document type declaration (errc::doctype_not_allowed).
/// Throws limit_error when its elements are nested deeper than limits.max_xml_depth (errc::xml_too_deep) or are more
/// than limits.max_xml_elements (errc::too_many_xml_elements). The limits count every element of the document.
[[nodiscard]] std::unique_ptr<pugi::xml_document> parse_xml(std::string_view text, std::uint64_t offset,
                                                            const limits& limits);

/// The character data of element: the text of its character data and CDATA children, in order.
[[nodiscard]] std::string character_data(const pugi::xml_node& element);

/// Where node starts in the text it was parsed from: the '<' of an element. Empty when pugixml cannot tell.
[[nodiscard]] std::optional<std::size_t> element_offset(const pugi::xml_node& element) noexcept;

/// The length of the element that starts at text[start], from its '<' to the end of its end tag. text is a document
/// that parse_xml() accepted. An element that does not end, which such a text cannot have, extends to the end of text.
[[nodiscard]] std::size_t element_length(std::string_view text, std::size_t start);

} // namespace openxisf::detail
