// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#pragma once

#include <openxisf/export.h>

#include <cstdint>

/// @file
/// Safety limits for reading untrusted units.

namespace openxisf {

/// Safety limits for reading units from untrusted sources. Exceeding one raises limit_error. Zero means no limit.
///
/// The defaults accept every unit a real application is expected to write. Lower them for services that open
/// files from unknown parties.
struct limits
{
    /// Size of the XML header, in bytes.
    std::uint64_t max_header_size = std::uint64_t{64} << 20;
    /// Nesting depth of XML elements.
    std::uint64_t max_xml_depth = 64;
    /// Number of XML elements in the header.
    std::uint64_t max_xml_elements = 1'000'000;
    /// Size of a single allocation, in bytes.
    std::uint64_t max_allocation = std::uint64_t{16} << 30;
    /// Data loaded when a unit is opened (ICC profiles, the pixels of thumbnails, and the values of properties and
    /// table cells in data blocks), in bytes. Each data block counts with the larger of its stored and its uncompressed
    /// size. The copies that Reference elements make, of what an image names for each image that names it and of an
    /// image listed again, count too, by the bytes they hold.
    std::uint64_t max_ancillary_data = std::uint64_t{256} << 20;
    /// Number of block index nodes in a data blocks file.
    std::uint64_t max_index_nodes = 65'536;
    /// Window size of Zstandard frames, in bytes, rounded down to a power of two of at least 1 KiB. Only frames that do
    /// not declare their size need a window buffer; the others are decoded in place, within max_allocation.
    std::uint64_t max_zstd_window = std::uint64_t{128} << 20;
};

} // namespace openxisf
