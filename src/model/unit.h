// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/error.h>
#include <openxisf/io.h>
#include <openxisf/limits.h>
#include <openxisf/reader.h>

#include "container/data_block.h"
#include "io/thread_safe_source.h"
#include "model/images.h"
#include "model/properties.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openxisf::detail {

/// An opened unit: everything that a reader holds. Opening reads and checks the header, describes the data blocks, and
/// reads the properties, the tables, the images and what describes them; problems confined to one object of the unit
/// are diagnostics (spec §7). Only load_ancillary_data() changes it after construction.
struct unit
{
    /// Opens the unit in input. Throws what the constructors of reader document.
    unit(std::unique_ptr<input_source> input, read_options requested);

    /// Opens the unit again from its source with the ancillary data loaded, as reader::load_ancillary_data() documents.
    /// The unit is unchanged when it throws.
    void load_ancillary_data();

    thread_safe_source source;
    /// The options that the unit was opened with; their limits also apply to what is read from it later.
    read_options options;
    openxisf::limits limits;
    /// True when the ancillary data in data blocks are loaded.
    bool ancillary_loaded = true;
    unit_storage storage = unit_storage::monolithic;
    /// The detached signature, kept as written.
    std::optional<std::string> signature{};
    std::vector<data_block> blocks{};
    /// The properties of the unit and its standalone properties; the images and thumbnails hold their own.
    unit_properties properties{};
    unit_images images{};
    std::vector<diagnostic> diagnostics{};

private:
    // Reads the unit from source, with or without the ancillary data, and replaces what the unit holds only once
    // everything is read.
    void open(bool load_ancillary);
};

} // namespace openxisf::detail
