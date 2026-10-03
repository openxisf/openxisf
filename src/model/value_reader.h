// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#pragma once

#include <openxisf/limits.h>
#include <openxisf/property.h>

#include "container/data_block.h"
#include "core/diagnostic_log.h"
#include "io/thread_safe_source.h"
#include "model/ancillary_budget.h"

#include <pugixml.hpp>

#include <cstdint>
#include <optional>
#include <string>

// The values of Property elements and of the Cell elements of tables, which serialize a value the same way (spec §11.1,
// §11.3.3).

namespace openxisf::detail {

/// An element that serializes a property value.
struct value_element
{
    pugi::xml_node node{};
    std::string path{};
    /// The data block of the element, when it has a location attribute.
    const data_block* block = nullptr;
    property_type type = property_type::string;
};

/// Reads values as their type serializes them (spec §11.1.4 to §11.1.9): scalars, complex numbers and TimePoints in a
/// value attribute; Strings as character data or in a data block; vectors and matrices in a data block, whose size must
/// agree with the length, rows and columns attributes. Data blocks are read from the source and decompressed, each
/// counted against the budget by the larger of its stored and its data size.
class value_reader
{
public:
    /// With load_blocks false, a value in a data block is not read: read() returns nothing for it, without a
    /// diagnostic.
    value_reader(const thread_safe_source& source, const limits& limits, ancillary_budget& budget, bool load_blocks,
                 diagnostic_log& log) noexcept
        : source_(source), limits_(limits), budget_(budget), load_blocks_(load_blocks), log_(log)
    {}

    /// The value of element, or nothing when it cannot be read. Every problem is recorded in log:
    /// - a value that its type cannot have, a value attribute missing where the type needs one, a vector or matrix
    ///   without a data block or whose length, rows or columns disagree with the size of its block, a String block that
    ///   is not UTF-8, a block that fails its checksum or does not decompress, and a block beyond the budget are
    ///   errors;
    /// - an attribute or data that the type does not use, a vector without a length attribute, and an empty vector or
    ///   matrix without a data block are warnings.
    ///
    /// What the source throws passes through.
    [[nodiscard]] std::optional<property_value> read(const value_element& element);

private:
    std::optional<property_value> read_value_attribute(const value_element& element);
    void ignore_value_attribute(const value_element& element);
    std::optional<property_value> read_string(const value_element& element);
    std::optional<property_value> read_vector(const value_element& element);
    std::optional<property_value> read_matrix(const value_element& element);
    bool read_count(const value_element& element, const char* name, std::optional<std::uint64_t>& count);
    bool check_size(const value_element& element, const block_descriptor& descriptor, std::uint64_t count,
                    const char* attribute);
    std::optional<std::vector<std::byte>> load(const value_element& element, const block_descriptor& descriptor);

    const thread_safe_source& source_;
    const limits& limits_;
    ancillary_budget& budget_;
    bool load_blocks_;
    diagnostic_log& log_;
};

} // namespace openxisf::detail
