// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/io.h>
#include <openxisf/reader.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
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

/// A block index element of a data blocks file (spec §9.4), as written. Each member can be changed to break one rule.
struct index_entry
{
    std::uint64_t id = 0;
    std::uint64_t position = 0;
    std::uint64_t length = 0;
    std::uint64_t uncompressed_length = 0;
    std::uint64_t reserved = 0;
};

/// A block index node, written at its position in a data blocks file.
struct index_node
{
    std::uint64_t position = 16;
    std::vector<index_entry> elements{};
    std::uint64_t next = 0;
    std::uint32_t reserved = 0;
    /// The number of elements that the node declares; the size of elements when empty.
    std::optional<std::uint32_t> length{};
};

/// Bytes written at a position of a constructed file.
struct placed_bytes
{
    std::uint64_t position = 0;
    std::vector<std::byte> data{};
};

/// A data blocks file of size bytes, zeros but for the signature, the reserved field, each node at its position and
/// each of blocks at its position. size grows to hold them when it is smaller.
[[nodiscard]] std::vector<std::byte> blocks_file(const std::vector<index_node>& nodes,
                                                 const std::vector<placed_bytes>& blocks = {}, std::uint64_t size = 0,
                                                 std::uint64_t reserved = 0);

/// A data blocks file whose single index node points to each of blocks, with the identifiers of ids, in order. The
/// blocks follow the index one after the other.
[[nodiscard]] std::vector<std::byte> blocks_file_of(const std::vector<std::vector<std::byte>>& blocks,
                                                    const std::vector<std::uint64_t>& ids);

/// A resolver of files in memory, which it finds by the location of their reference, whatever its form. It records
/// the location of each file it opens in opened, when that is given, and returns null for any other location.
[[nodiscard]] external_resolver memory_resolver(std::map<std::string, std::vector<std::byte>, std::less<>> files,
                                                std::shared_ptr<std::vector<std::string>> opened = nullptr);

/// Opens the unit in memory.
[[nodiscard]] reader open_unit(std::vector<std::byte> unit, read_options options = {});

/// Opens a monolithic file that holds header.
[[nodiscard]] reader open_header(std::string_view header, read_options options = {});

} // namespace openxisf::test
