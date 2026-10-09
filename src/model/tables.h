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
#include <unordered_map>
#include <vector>

// The tables of a unit (spec §8.4.4.7, §11.2, §11.3): every Table element of its header, read once, with its structure
// and its cells.

namespace openxisf::detail {

/// The tables of a unit, by the index of their elements.
struct unit_tables
{
    /// The tables that could be read.
    std::unordered_map<std::size_t, table> tables{};
    /// The identifier of each table that is not read because a cell has its value left in its data block
    /// (value_reader::defers()).
    std::unordered_map<std::size_t, std::string> deferred_ids{};
};

/// Reads every Table element of outline, and returns the tables that could be read, by the index of their element. The
/// structure of a table is its Structure element, or the standalone Structure that its Reference element names, whose
/// fields are copied into each table that names it, each copy counted against budget. The cells are read with values,
/// as Property elements of the types of the fields; a table with a cell whose value values leaves in its data block is
/// not read beyond that cell, but keeps its identifier. Every problem is recorded in log, and a table that cannot be
/// read is unavailable:
/// - a Table element without an id, a structure, or with more than one; a Reference that names something other than a
///   standalone Structure element; a structure without fields, or with a field without an id or a type, of a type that
///   the specification does not define, or with the identifier of another field; a row that does not have one cell for
///   each field; a cell whose value cannot be read (value_reader::read()); rows or columns attributes that are
///   malformed or disagree with the table; and a copy of a structure beyond the budget are errors;
/// - an identifier of a table or a field that is not of the grammar of spec §8.4.1, a malformed format specifier of a
///   field or one on a TimePoint field, an id, type or format attribute on a Cell element, and a standalone Structure
///   element without a uid are warnings.
[[nodiscard]] unit_tables read_tables(const unit_outline& outline, const std::vector<data_block>& blocks,
                                      value_reader& values, ancillary_budget& budget, diagnostic_log& log);

} // namespace openxisf::detail
