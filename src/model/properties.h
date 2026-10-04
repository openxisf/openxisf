// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/property.h>

#include "container/data_block.h"
#include "core/diagnostic_log.h"
#include "model/ancillary_budget.h"
#include "model/outline.h"
#include "model/value_reader.h"

#include <cstddef>
#include <string>
#include <vector>

// The properties of a unit (spec §8.4, §11.1, §11.4): every Property element of its header, read once, and the objects
// that the properties belong to.

namespace openxisf::detail {

/// The properties of an Image or Thumbnail element.
struct element_properties
{
    /// The element in the outline of the unit, and its path.
    std::size_t element = no_element;
    std::string path{};
    property_list properties{};
};

/// The properties of a unit.
struct unit_properties
{
    /// The properties of the unit itself: those of its Metadata element (spec §11.4).
    property_list metadata{};
    /// The standalone properties: the Property elements of the root element (spec §11.1).
    property_list standalone{};
    /// The properties of each Image and Thumbnail element, in document order.
    std::vector<element_properties> objects{};
};

/// Reads every Property element of outline, its value with values, and gives each object the properties that it
/// contains and those that its Reference elements name (spec §11.1). A property of several objects is copied into each
/// but the last, and each copy counts against budget by its held_size(), so that References cannot multiply the memory
/// that a unit takes. Every problem is recorded in log, and a property that cannot be read is unavailable:
/// - a Property element without an id or a type attribute, a type that the specification does not define, a value that
///   cannot be read (value_reader::read()), or a copy beyond the budget are errors, and so is a second property with
///   the identifier of another of the same object;
/// - an identifier that is not of the grammar of spec §8.4.1, a reserved identifier of another type than the
///   specification gives it, and a malformed format specifier or one on a TimePoint are warnings;
/// - no Metadata element, more than one, a missing mandatory metadata property (spec §11.4.1), a property outside the
///   XISF namespace in the Metadata element, and a Reference in it that names no Property element are warnings.
[[nodiscard]] unit_properties read_properties(const unit_outline& outline, const std::vector<data_block>& blocks,
                                              value_reader& values, ancillary_budget& budget, diagnostic_log& log);

} // namespace openxisf::detail
