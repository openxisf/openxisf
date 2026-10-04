// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "model/property_types.h"

#include <array>

namespace openxisf::detail {

namespace {

struct type_entry
{
    property_type type{};
    std::string_view name{};
    std::string_view alternate{};
    type_category category{};
    /// The element type of a vector or matrix; the type itself otherwise.
    property_type element{};
    /// The size of a value of a scalar or complex type.
    std::size_t size = 0;
};

using enum property_type;

// The categories without a namesake among the types.
constexpr type_category scalar = type_category::scalar;
constexpr type_category complex = type_category::complex;
constexpr type_category vector = type_category::vector;
constexpr type_category matrix = type_category::matrix;

// In the order of property_type. The names are those of spec Tables 3 to 8.
constexpr std::array<type_entry, 51> types{{
    {.type = boolean, .name = "Boolean", .category = scalar, .element = boolean, .size = 1},
    {.type = int8, .name = "Int8", .category = scalar, .element = int8, .size = 1},
    {.type = uint8, .name = "UInt8", .alternate = "Byte", .category = scalar, .element = uint8, .size = 1},
    {.type = int16, .name = "Int16", .alternate = "Short", .category = scalar, .element = int16, .size = 2},
    {.type = uint16, .name = "UInt16", .alternate = "UShort", .category = scalar, .element = uint16, .size = 2},
    {.type = int32, .name = "Int32", .alternate = "Int", .category = scalar, .element = int32, .size = 4},
    {.type = uint32, .name = "UInt32", .alternate = "UInt", .category = scalar, .element = uint32, .size = 4},
    {.type = int64, .name = "Int64", .category = scalar, .element = int64, .size = 8},
    {.type = uint64, .name = "UInt64", .category = scalar, .element = uint64, .size = 8},
    {.type = int128, .name = "Int128", .category = scalar, .element = int128, .size = 16},
    {.type = uint128, .name = "UInt128", .category = scalar, .element = uint128, .size = 16},
    {.type = float32, .name = "Float32", .alternate = "Float", .category = scalar, .element = float32, .size = 4},
    {.type = float64, .name = "Float64", .alternate = "Double", .category = scalar, .element = float64, .size = 8},
    {.type = float128, .name = "Float128", .alternate = "Quad", .category = scalar, .element = float128, .size = 16},
    {.type = complex32, .name = "Complex32", .category = complex, .element = complex32, .size = 8},
    {.type = complex64,
     .name = "Complex64",
     .alternate = "Complex",
     .category = complex,
     .element = complex64,
     .size = 16},
    {.type = complex128, .name = "Complex128", .category = complex, .element = complex128, .size = 32},
    {.type = property_type::string,
     .name = "String",
     .category = type_category::string,
     .element = property_type::string},
    {.type = property_type::time_point,
     .name = "TimePoint",
     .category = type_category::time_point,
     .element = property_type::time_point},
    {.type = i8_vector, .name = "I8Vector", .category = vector, .element = int8},
    {.type = ui8_vector, .name = "UI8Vector", .alternate = "ByteArray", .category = vector, .element = uint8},
    {.type = i16_vector, .name = "I16Vector", .category = vector, .element = int16},
    {.type = ui16_vector, .name = "UI16Vector", .category = vector, .element = uint16},
    {.type = i32_vector, .name = "I32Vector", .alternate = "IVector", .category = vector, .element = int32},
    {.type = ui32_vector, .name = "UI32Vector", .alternate = "UIVector", .category = vector, .element = uint32},
    {.type = i64_vector, .name = "I64Vector", .category = vector, .element = int64},
    {.type = ui64_vector, .name = "UI64Vector", .category = vector, .element = uint64},
    {.type = i128_vector, .name = "I128Vector", .category = vector, .element = int128},
    {.type = ui128_vector, .name = "UI128Vector", .category = vector, .element = uint128},
    {.type = f32_vector, .name = "F32Vector", .category = vector, .element = float32},
    {.type = f64_vector, .name = "F64Vector", .alternate = "Vector", .category = vector, .element = float64},
    {.type = f128_vector, .name = "F128Vector", .category = vector, .element = float128},
    {.type = c32_vector, .name = "C32Vector", .category = vector, .element = complex32},
    {.type = c64_vector, .name = "C64Vector", .category = vector, .element = complex64},
    {.type = c128_vector, .name = "C128Vector", .category = vector, .element = complex128},
    {.type = i8_matrix, .name = "I8Matrix", .category = matrix, .element = int8},
    {.type = ui8_matrix, .name = "UI8Matrix", .alternate = "ByteMatrix", .category = matrix, .element = uint8},
    {.type = i16_matrix, .name = "I16Matrix", .category = matrix, .element = int16},
    {.type = ui16_matrix, .name = "UI16Matrix", .category = matrix, .element = uint16},
    {.type = i32_matrix, .name = "I32Matrix", .alternate = "IMatrix", .category = matrix, .element = int32},
    {.type = ui32_matrix, .name = "UI32Matrix", .alternate = "UIMatrix", .category = matrix, .element = uint32},
    {.type = i64_matrix, .name = "I64Matrix", .category = matrix, .element = int64},
    {.type = ui64_matrix, .name = "UI64Matrix", .category = matrix, .element = uint64},
    {.type = i128_matrix, .name = "I128Matrix", .category = matrix, .element = int128},
    {.type = ui128_matrix, .name = "UI128Matrix", .category = matrix, .element = uint128},
    {.type = f32_matrix, .name = "F32Matrix", .category = matrix, .element = float32},
    {.type = f64_matrix, .name = "F64Matrix", .alternate = "Matrix", .category = matrix, .element = float64},
    {.type = f128_matrix, .name = "F128Matrix", .category = matrix, .element = float128},
    {.type = c32_matrix, .name = "C32Matrix", .category = matrix, .element = complex32},
    {.type = c64_matrix, .name = "C64Matrix", .category = matrix, .element = complex64},
    {.type = c128_matrix, .name = "C128Matrix", .category = matrix, .element = complex128},
}};

constexpr bool types_in_order() noexcept
{
    for (std::size_t i = 0; i < types.size(); ++i) {
        if (static_cast<std::size_t>(types[i].type) != i) {
            return false;
        }
    }
    return static_cast<std::size_t>(c128_matrix) + 1 == types.size();
}
static_assert(types_in_order());

const type_entry& entry_of(property_type type) noexcept
{
    return types[static_cast<std::size_t>(type)];
}

} // namespace

type_category category_of(property_type type) noexcept
{
    return entry_of(type).category;
}

std::optional<property_type> property_type_named(std::string_view name) noexcept
{
    for (const type_entry& entry : types) {
        if (entry.name == name || (!entry.alternate.empty() && entry.alternate == name)) {
            return entry.type;
        }
    }
    return std::nullopt;
}

std::string_view alternate_type_name(property_type type) noexcept
{
    return entry_of(type).alternate;
}

property_type element_type(property_type type) noexcept
{
    return entry_of(type).element;
}

std::size_t value_size(property_type type) noexcept
{
    return entry_of(type).size;
}

std::size_t byte_order_item_size(property_type type) noexcept
{
    const type_entry& entry = entry_of(type);
    return entry.category == complex ? entry.size / 2 : entry.size;
}

std::string_view type_name(property_type type) noexcept
{
    return entry_of(type).name;
}

} // namespace openxisf::detail
