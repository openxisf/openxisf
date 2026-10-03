// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#pragma once

#include <openxisf/limits.h>

#include "core/diagnostic_log.h"

#include <pugixml.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace openxisf::detail {

/// An XISF header, parsed and checked down to its root element (spec §9.5).
struct parsed_header
{
    std::unique_ptr<pugi::xml_document> document{};
    /// The XISF root element.
    pugi::xml_node root{};
    /// The detached XML signature that follows the root element, exactly as written in the header. Empty when the unit
    /// is not signed.
    std::optional<std::string> signature{};
};

/// Parses the header of a unit, which starts at offset in its source, and checks its XML declaration, its root element
/// and what follows the root element. A UTF-8 byte order mark at the start is skipped.
///
/// Throws what parse_xml() throws; invalid_data_error when the declaration claims another encoding than UTF-8 for a
/// header beyond ASCII (errc::invalid_xml), or when there is no XISF root element or more than one
/// (errc::invalid_root_element); and unsupported_error when the root element is of another version than 1.0
/// (errc::unsupported_version). Tolerated problems are recorded in log.
[[nodiscard]] parsed_header parse_header(std::string_view text, std::uint64_t offset, const limits& limits,
                                         diagnostic_log& log);

} // namespace openxisf::detail
