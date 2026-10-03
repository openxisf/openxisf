// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#pragma once

#include <openxisf/reader.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Constructed units, built from XML text and bytes with the standard library alone, so that the tests of the reader
// never depend on the library's own writer.

namespace openxisf::test {

/// The XML declaration that starts an XISF header (spec §9.5).
inline constexpr std::string_view xml_declaration = R"(<?xml version="1.0" encoding="UTF-8"?>)";

/// The start tag of an XISF root element, with the namespace and schema attributes of spec §9.5.
inline constexpr std::string_view root_start_tag =
    R"(<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf")"
    R"( xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance")"
    R"( xsi:schemaLocation="http://www.pixinsight.com/xisf http://pixinsight.com/xisf/xisf-1.0.xsd">)";

/// The mandatory properties of a Metadata element (spec §11.4.1).
inline constexpr std::string_view mandatory_metadata =
    R"(<Property id="XISF:CreationTime" type="TimePoint" value="2026-10-02T12:00:00Z"/>)"
    R"(<Property id="XISF:CreatorApplication" type="String">OpenXISF tests 1.0</Property>)";

/// A header: the XML declaration, and a root element that holds a Metadata element followed by body. The Metadata
/// element has the mandatory properties followed by metadata.
[[nodiscard]] std::string header_xml(std::string_view body = {}, std::string_view metadata = {});

/// An Image element that is valid on its own: one 8-bit gray pixel in an embedded block. attributes go into its start
/// tag, and content after its Data element.
[[nodiscard]] std::string image_xml(std::string_view attributes = {}, std::string_view content = {});

/// A Thumbnail element that is valid on its own, like image_xml().
[[nodiscard]] std::string thumbnail_xml(std::string_view attributes = {}, std::string_view content = {});

/// A FITSKeyword element that is valid on its own, with the given attributes added.
[[nodiscard]] std::string keyword_xml(std::string_view attributes = {});

/// The first 16 bytes of a monolithic file (spec §9.2). Each member can be changed to break one rule.
struct file_preamble
{
    std::string signature = "XISF0100";
    /// The header length; the length of the header when empty.
    std::optional<std::uint32_t> header_length{};
    std::uint32_t reserved = 0;
};

/// A monolithic file that holds header, with nothing after it.
[[nodiscard]] std::vector<std::byte> monolithic_file(std::string_view header, const file_preamble& preamble = {});

/// A monolithic file with attached blocks. Every {N} in header becomes the position and the size of blocks[N], so
/// that location="attachment:{0}" locates the first block. The blocks follow the header one after the other, from the
/// first multiple of 4096 after it, with zeros in between.
[[nodiscard]] std::vector<std::byte> file_with_attachments(std::string_view header,
                                                           const std::vector<std::vector<std::byte>>& blocks);

/// Opens the unit in memory.
[[nodiscard]] reader open_unit(std::vector<std::byte> unit, read_options options = {});

/// Opens a monolithic file that holds header.
[[nodiscard]] reader open_header(std::string_view header, read_options options = {});

} // namespace openxisf::test
