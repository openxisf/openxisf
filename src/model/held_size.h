// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/image.h>
#include <openxisf/property.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// What a copy of a value of a unit takes, counted against limits::max_ancillary_data so that Reference elements cannot
// multiply the memory that a unit takes: the object itself, and the bytes that it holds beyond its own size.

namespace openxisf::detail {

/// The identifier, the comment, the unit of the format, and the characters of a String or the elements of a vector or
/// matrix.
[[nodiscard]] std::uint64_t heap_size(const property& item);

/// The identifier, caption and comment, the fields with their texts, and the rows with their cells and the values of
/// the cells.
[[nodiscard]] std::uint64_t heap_size(const table& item);

/// The name, value and comment.
[[nodiscard]] std::uint64_t heap_size(const fits_keyword& item);

/// The bytes, those of an ICC profile.
[[nodiscard]] std::uint64_t heap_size(const std::vector<std::byte>& bytes);

/// The name.
[[nodiscard]] std::uint64_t heap_size(const rgb_working_space& item);

/// The name.
[[nodiscard]] std::uint64_t heap_size(const display_function& item);

/// The pattern and the name.
[[nodiscard]] std::uint64_t heap_size(const color_filter_array& item);

/// Nothing: a resolution has a fixed size.
[[nodiscard]] std::uint64_t heap_size(const resolution& item);

/// The pixel data, and what the thumbnail holds besides: its identifiers, dimensions, properties, tables, keywords, ICC
/// profile and the names of its elements.
[[nodiscard]] std::uint64_t heap_size(const thumbnail& item);

/// What the image holds: its identifiers, dimensions, properties, tables, keywords, ICC profile, the names and pattern
/// of its elements, and what its thumbnail holds.
[[nodiscard]] std::uint64_t heap_size(const image_info& item);

/// What a copy of item takes: the object, and what it holds beyond its own size (heap_size()).
template <typename T> [[nodiscard]] std::uint64_t held_size(const T& item)
{
    return sizeof(T) + heap_size(item);
}

} // namespace openxisf::detail
