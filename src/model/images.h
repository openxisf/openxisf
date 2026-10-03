// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#pragma once

#include <openxisf/image.h>
#include <openxisf/limits.h>
#include <openxisf/property.h>

#include "container/data_block.h"
#include "core/diagnostic_log.h"
#include "io/thread_safe_source.h"
#include "model/ancillary.h"
#include "model/ancillary_budget.h"
#include "model/outline.h"
#include "model/properties.h"

#include <cstddef>
#include <unordered_map>
#include <vector>

// The images of a unit (spec §11.5): every Image and Thumbnail element of its header, read once, with what is
// associated with it (spec §11.6 to §11.12), and the root-level Reference elements that list an image again (spec
// §11.13).

namespace openxisf::detail {

/// The objects read from the elements of a header that the images and the unit are made of. read_images() moves them
/// into the images.
struct unit_objects
{
    unit_properties properties{};
    /// The table of each Table element that could be read, by the index of its element.
    std::unordered_map<std::size_t, table> tables{};
    ancillary_elements ancillary{};
};

/// The images of a unit, in the order of reader::images(), and their pixel data, and the standalone tables.
struct unit_images
{
    std::vector<image_info> infos{};
    /// For each image, the index of its data block in the blocks of the unit. Several images can have the same one.
    std::vector<std::size_t> blocks{};
    /// The tables of the root element, in document order.
    std::vector<table> tables{};
};

/// Reads every Image and Thumbnail element of outline, with its data block among blocks, and lists the images: each
/// Image element, and the image that each Reference element of the root element names, where the Reference is.
///
/// Each Image and Thumbnail element takes what is associated with it from objects: its properties, and the tables,
/// FITS keywords, ICC profile, RGB working space, display function, resolution, and for an image its colour filter
/// array and thumbnail, that it contains or that its Reference elements name. The pixel data of a thumbnail are loaded
/// from source within budget, unless load_blocks is false. An object associated with several images is copied into
/// each but the last, and so is an image listed again; each copy counts against budget by its held_size(). Every
/// problem is recorded in log:
/// - an Image or Thumbnail element without a geometry or a sampleFormat attribute or a data block, a geometry or offset
///   attribute that cannot be read, a sample format, colour space or pixel storage that the specification does not
///   define, fewer channels than the colour space has nominal channels, and a data block that does not hold exactly the
///   pixel data of the geometry are errors, and leave the image or thumbnail out;
/// - a thumbnail that is not two-dimensional, of UInt8 or UInt16 samples, Gray or RGB, with at most one alpha channel,
///   or whose pixel data cannot be loaded, is an error, and is left out;
/// - a table with the identifier of a property or an earlier table of its object is an error, and is left out;
/// - a copy beyond the budget is an error, and the association or listing is left out;
/// - a floating point image without bounds, malformed bounds or bounds on a thumbnail, an unknown image type or
///   orientation, a negative offset, an id outside its grammar or of another image, and a uuid that is malformed or not
///   of version 4 are warnings;
/// - a second ICC profile, RGB working space, display function, colour filter array, resolution or thumbnail of an
///   object, and a colour filter array of an image that is not two-dimensional, are warnings, and are ignored;
/// - a Reference of the root element that names something other than an Image element, and a Reference of an Image or
///   Thumbnail element that names something that cannot be associated with it, are warnings, and are ignored.
///
/// An image whose data block is unavailable is listed: its pixel data cannot be read, and the block has its own error.
[[nodiscard]] unit_images read_images(const unit_outline& outline, const std::vector<data_block>& blocks,
                                      unit_objects& objects, const thread_safe_source& source, const limits& limits,
                                      ancillary_budget& budget, bool load_blocks, diagnostic_log& log);

} // namespace openxisf::detail
