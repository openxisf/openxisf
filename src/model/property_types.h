// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/property.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// The property types of spec §8.4.4: their names, and how a Property element serializes their values (spec §11.1).

namespace openxisf::detail {

/// How a Property element serializes values of a type.
enum class type_category : std::uint8_t
{
    scalar,     ///< In a value attribute (spec §11.1.4).
    complex,    ///< In a value attribute, as (real,imag) (spec §11.1.5).
    string,     ///< In the character data, or in a data block (spec §11.1.6).
    time_point, ///< In a value attribute (spec §11.1.7).
    vector,     ///< In a data block, with a length attribute (spec §11.1.8).
    matrix,     ///< In a data block, with rows and columns attributes (spec §11.1.9).
};

/// The name of a type in the specification, such as "Float32"; property_type_name() of the public API.
[[nodiscard]] std::string_view type_name(property_type type) noexcept;

/// The category of a type.
[[nodiscard]] type_category category_of(property_type type) noexcept;

/// The type with the given name or alternate name of spec Tables 3 to 8, such as "UInt8" or "Byte". Names are
/// case-sensitive. Empty for any other name.
[[nodiscard]] std::optional<property_type> property_type_named(std::string_view name) noexcept;

/// The alternate name of a type, such as "Byte" for UInt8, or empty when it has none.
[[nodiscard]] std::string_view alternate_type_name(property_type type) noexcept;

/// The type of the elements of a vector or matrix type, such as UInt16 for UI16Vector; any other type itself.
[[nodiscard]] property_type element_type(property_type type) noexcept;

/// The size in bytes of a value of a scalar or complex type in a data block: 1 for Boolean and Int8, 8 for Complex32,
/// 32 for Complex128. 0 for the other types.
[[nodiscard]] std::size_t value_size(property_type type) noexcept;

/// The size in bytes of the items whose byte order a data block of values of a scalar or complex type follows (spec
/// §10.4): the size of a scalar, or of one part of a complex number.
[[nodiscard]] std::size_t byte_order_item_size(property_type type) noexcept;

} // namespace openxisf::detail
