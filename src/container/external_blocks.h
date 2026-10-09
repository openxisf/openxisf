// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/io.h>
#include <openxisf/limits.h>

#include "container/block_attributes.h"
#include "container/data_block.h"
#include "core/diagnostic_log.h"

#include <string>
#include <vector>

// The external data blocks of a distributed unit (spec §10.2, §10.3): the files that hold them, opened through a
// resolver, and their places in those files.

namespace openxisf::detail {

/// The external reference of a url() or path() location, which a resolver opens.
[[nodiscard]] external_reference reference_of(const block_location& location);

/// The location of a file as a unit writes it, for messages: url(URL), path(/path) or path(@header_dir/path).
[[nodiscard]] std::string location_text(const external_reference& reference);

/// Gives each available external block of blocks the file that holds it, and its place in that file. Each file is
/// opened through resolver once, in the order of the first block that names it, and the block index of a data blocks
/// file is read once, for the identifiers that the blocks name (read_block_index()). A block that cannot be located
/// becomes unavailable, with an error in log about its location attribute:
/// - no resolver, a null source, the io_error or unsupported_error of the resolver or of the source, more files than
///   limits.max_external_files, and a data blocks file that cannot be read or whose index is malformed; the block keeps
///   the class and the system error of an exception of the resolver, and the io_error of the source, for a read of it
///   (data_block::origin), and so does the exception of a strict log;
/// - an index-id that no element has, or only a free one, or that several elements have;
/// - an element whose block is not inside the file after its first 16 bytes, whose uncompressed length is not zero
///   for an uncompressed block, or differs from the uncompressed size of a compressed one;
/// - subblocks whose sizes do not add up to the stored and uncompressed sizes of the block.
///
/// Reserved fields of a data blocks file that are not zero are a warning for each file, and so are an empty block and
/// an element of a compressed block without its uncompressed length. Other exceptions of the resolver and of the
/// sources pass through.
void locate_external_blocks(std::vector<data_block>& blocks, const external_resolver& resolver, const limits& limits,
                            diagnostic_log& log);

} // namespace openxisf::detail
