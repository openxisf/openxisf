// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/io.h>
#include <openxisf/property.h>
#include <openxisf/writer.h>

#include "codec/compression.h"
#include "model/unit_contents.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

// Monolithic files as the writer writes them (spec §9.2): the header at byte 16, then the attached data blocks, each at
// a multiple of the block alignment, with zeros in the unused space.

namespace openxisf::detail {

/// What a save writes besides the model: the values of the generated metadata that do not follow from the blocks.
struct save_context
{
    /// XISF:CreationTime.
    date_time creation_time{};
    /// The uuid attribute of each image, empty for none.
    std::span<const std::string> uuids{};
};

/// The name of the operating system for XISF:CreatorOS (spec §11.4.2, Table 10), or empty when it has none there.
[[nodiscard]] std::string_view creator_os() noexcept;

/// The abstract level (spec §11.4.2) that XISF:CompressionLevel reports for a compression level option: level itself,
/// or for 0 the abstract level that gives the default level of the codec. Empty for LZ4, which has no levels.
[[nodiscard]] std::optional<int> reported_compression_level(compression_codec codec, int level) noexcept;

/// Writes unit, which validate_unit() accepted, to sink, and finishes the sink.
///
/// When every attached block is stored as it is, without compression or checksum, their sizes are known, so the
/// length of the header follows by fixed-point iteration (positions are decimal numbers, whose length changes the
/// length of the header), and the unit is written in order. Otherwise a sink that can rewrite gets room for the
/// longest header that the blocks can give, each attached block as it is compressed and hashed, subblock by subblock,
/// and the header last, in that room; and any other sink gets the unit in order, after every attached block has been
/// compressed into memory or hashed.
///
/// Throws validation_error with errc::header_too_large when the header does not fit in the 32 bits of its length;
/// cancelled_error when options.progress returns false; and what the sink and the progress function throw.
void write_unit(const unit_contents& unit, const write_options& options, const save_context& context,
                output_sink& sink);

} // namespace openxisf::detail
