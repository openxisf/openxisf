// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <string>
#include <string_view>

// XML 1.0 text as an encoder writes it: what text an XML document can hold, and its escaped forms.

namespace openxisf::detail {

/// True when text is valid UTF-8 and holds only characters that XML 1.0 allows (its Char production): no U+0000, no
/// control character other than tab, line feed and carriage return, no surrogate, and neither U+FFFE nor U+FFFF. Other
/// text cannot be written in an XML document, not even as a character reference.
[[nodiscard]] bool is_xml_text(std::string_view text) noexcept;

/// Appends text, which is_xml_text() accepts, as an attribute value between double quotes. The markup characters are
/// escaped, and tab, line feed and carriage return become character references, since a parser would turn them into
/// spaces otherwise (attribute-value normalization).
void append_attribute_value(std::string& output, std::string_view text);

/// Appends text, which is_xml_text() accepts, as character data. The markup characters are escaped, and a carriage
/// return becomes a character reference, since a parser would turn it into a line feed otherwise (end-of-line
/// handling).
void append_character_data(std::string& output, std::string_view text);

} // namespace openxisf::detail
