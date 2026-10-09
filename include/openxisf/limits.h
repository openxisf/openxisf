// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/export.h>

#include <cstdint>

/// @file
/// Safety limits for reading untrusted units.

namespace openxisf {

/// Safety limits for reading units from untrusted sources. Zero means no limit.
///
/// A header beyond max_header_size, max_xml_depth or max_xml_elements fails the open with limit_error. What an open
/// loads beyond the other limits is left unavailable, with an error diagnostic, as other problems confined to one
/// object are: ancillary data, and the external files of a distributed unit and their block indexes. limit_error is
/// then thrown by a strict open (read_options::strict), and by a later read of what was left unavailable, such as
/// reader::read_pixels() of an image whose data block was not located. A read of pixel data beyond max_allocation or
/// max_zstd_window throws limit_error.
///
/// The defaults accept every unit a real application is expected to write. Lower them for services that open
/// files from unknown parties.
struct limits
{
    /// Size of the XML header, in bytes. It bounds the memory of the parse, which takes up to about 13 times the size
    /// of a header made of attributes: each takes about 40 bytes besides its text, and no other limit counts them, as
    /// max_xml_elements counts the elements, which take about 64 bytes each.
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
    /// Number of external files that the data blocks of a distributed unit name, each opened once and kept open while
    /// the unit is. reader::load_ancillary_data() opens them again before it closes the first ones.
    std::uint64_t max_external_files = 256;
    /// Window size of Zstandard frames, in bytes, rounded down to a power of two of at least 1 KiB. Only frames that do
    /// not declare their size need a window buffer; the others are decoded in place, within max_allocation. A read
    /// decodes such frames one at a time, also where it decodes the other subblocks of a block in parallel.
    std::uint64_t max_zstd_window = std::uint64_t{128} << 20;
};

} // namespace openxisf
