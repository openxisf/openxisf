// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#pragma once

#include <openxisf/property.h>

#include "container/block_attributes.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Property values in data blocks (spec §11.1.6, §11.1.8, §11.1.9): String values as UTF-8, and the elements of vectors
// and matrices one after the other, matrices in row order, each element in the byte order of the block (spec §10.4).

namespace openxisf::detail {

/// The size of the data of count elements of the vector or matrix type type, or nothing when it does not fit in 64
/// bits.
[[nodiscard]] std::optional<std::uint64_t> elements_size(property_type type, std::uint64_t count) noexcept;

/// The value of a vector of type type from the data of its block, whose size must be a multiple of the size of an
/// element. Throws usage_error otherwise.
[[nodiscard]] property_value decode_vector(property_type type, std::span<const std::byte> data, byte_order order);

/// The value of a matrix of type type from the data of its block, whose size must be that of rows × columns elements.
/// Throws usage_error otherwise.
[[nodiscard]] property_value decode_matrix(property_type type, std::uint64_t rows, std::uint64_t columns,
                                           std::span<const std::byte> data, byte_order order);

/// The value of a String from the data of its block. Throws invalid_data_error with errc::invalid_utf8 when the data
/// are not UTF-8, or contain U+0000.
[[nodiscard]] std::string decode_string(std::span<const std::byte> data);

/// The data of the elements of a vector or matrix value, in byte order order. Throws usage_error for another value.
[[nodiscard]] std::vector<std::byte> encode_elements(const property_value& value, byte_order order);

} // namespace openxisf::detail
