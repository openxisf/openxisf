// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/writer.h>

#include <pugixml.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Units written by the writer of the library, for its tests. Every unit written in tests can be recorded, with its
// header apart, for the XML schema of XISF, which only CI downloads, and for other XISF readers: set
// OPENXISF_WRITTEN_UNITS_DIR to an existing directory.

namespace openxisf::test {

/// The sinks that the writer writes in different ways.
enum class sink_kind
{
    rewritable,  ///< A memory_sink, which can rewrite: the header is written last.
    append_only, ///< A callback_sink without a rewrite function: the unit is written in order.
};

/// The header of a monolithic file: the bytes that its header length counts after the first 16. Fails the test when
/// the file is too short for them.
[[nodiscard]] std::string header_of(std::span<const std::byte> file);

/// The header of a monolithic file, parsed.
[[nodiscard]] std::unique_ptr<pugi::xml_document> parsed_header(std::span<const std::byte> file);

/// Writes a monolithic file, and its header apart, to the directory of OPENXISF_WRITTEN_UNITS_DIR, when it is set,
/// named after the running test: name.xisf and name.xml.
void record_unit(std::span<const std::byte> file);

/// The unit that output writes into a sink of the given kind, recorded with record_unit().
[[nodiscard]] std::vector<std::byte> written(const writer& output, sink_kind kind = sink_kind::rewritable);

/// The first element at a path of element names below the root element, such as "Image/Property", or an empty node.
[[nodiscard]] pugi::xml_node element_at(const pugi::xml_document& header, std::string_view path);

} // namespace openxisf::test
