// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

// The property types of spec §8.4.4, Tables 3 to 8: names, alternate names, and their serialization.

#include "model/property_types.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>

namespace {

using openxisf::property_type;
using openxisf::property_type_name;
using openxisf::detail::alternate_type_name;
using openxisf::detail::byte_order_item_size;
using openxisf::detail::category_of;
using openxisf::detail::element_type;
using openxisf::detail::property_type_named;
using openxisf::detail::type_category;
using openxisf::detail::value_size;

struct named_type
{
    std::string_view name{};
    std::string_view alternate{};
    property_type type{};
};

// Every row of spec Tables 3 to 8, in their order.
constexpr std::array<named_type, 51> specification_types{{
    {.name = "Boolean", .type = property_type::boolean},
    {.name = "Int8", .type = property_type::int8},
    {.name = "UInt8", .alternate = "Byte", .type = property_type::uint8},
    {.name = "Int16", .alternate = "Short", .type = property_type::int16},
    {.name = "UInt16", .alternate = "UShort", .type = property_type::uint16},
    {.name = "Int32", .alternate = "Int", .type = property_type::int32},
    {.name = "UInt32", .alternate = "UInt", .type = property_type::uint32},
    {.name = "Int64", .type = property_type::int64},
    {.name = "UInt64", .type = property_type::uint64},
    {.name = "Int128", .type = property_type::int128},
    {.name = "UInt128", .type = property_type::uint128},
    {.name = "Float32", .alternate = "Float", .type = property_type::float32},
    {.name = "Float64", .alternate = "Double", .type = property_type::float64},
    {.name = "Float128", .alternate = "Quad", .type = property_type::float128},
    {.name = "Complex32", .type = property_type::complex32},
    {.name = "Complex64", .alternate = "Complex", .type = property_type::complex64},
    {.name = "Complex128", .type = property_type::complex128},
    {.name = "String", .type = property_type::string},
    {.name = "TimePoint", .type = property_type::time_point},
    {.name = "I8Vector", .type = property_type::i8_vector},
    {.name = "UI8Vector", .alternate = "ByteArray", .type = property_type::ui8_vector},
    {.name = "I16Vector", .type = property_type::i16_vector},
    {.name = "UI16Vector", .type = property_type::ui16_vector},
    {.name = "I32Vector", .alternate = "IVector", .type = property_type::i32_vector},
    {.name = "UI32Vector", .alternate = "UIVector", .type = property_type::ui32_vector},
    {.name = "I64Vector", .type = property_type::i64_vector},
    {.name = "UI64Vector", .type = property_type::ui64_vector},
    {.name = "I128Vector", .type = property_type::i128_vector},
    {.name = "UI128Vector", .type = property_type::ui128_vector},
    {.name = "F32Vector", .type = property_type::f32_vector},
    {.name = "F64Vector", .alternate = "Vector", .type = property_type::f64_vector},
    {.name = "F128Vector", .type = property_type::f128_vector},
    {.name = "C32Vector", .type = property_type::c32_vector},
    {.name = "C64Vector", .type = property_type::c64_vector},
    {.name = "C128Vector", .type = property_type::c128_vector},
    {.name = "I8Matrix", .type = property_type::i8_matrix},
    {.name = "UI8Matrix", .alternate = "ByteMatrix", .type = property_type::ui8_matrix},
    {.name = "I16Matrix", .type = property_type::i16_matrix},
    {.name = "UI16Matrix", .type = property_type::ui16_matrix},
    {.name = "I32Matrix", .alternate = "IMatrix", .type = property_type::i32_matrix},
    {.name = "UI32Matrix", .alternate = "UIMatrix", .type = property_type::ui32_matrix},
    {.name = "I64Matrix", .type = property_type::i64_matrix},
    {.name = "UI64Matrix", .type = property_type::ui64_matrix},
    {.name = "I128Matrix", .type = property_type::i128_matrix},
    {.name = "UI128Matrix", .type = property_type::ui128_matrix},
    {.name = "F32Matrix", .type = property_type::f32_matrix},
    {.name = "F64Matrix", .alternate = "Matrix", .type = property_type::f64_matrix},
    {.name = "F128Matrix", .type = property_type::f128_matrix},
    {.name = "C32Matrix", .type = property_type::c32_matrix},
    {.name = "C64Matrix", .type = property_type::c64_matrix},
    {.name = "C128Matrix", .type = property_type::c128_matrix},
}};

TEST(property_types, every_name_and_alternate_name_of_the_specification)
{
    for (const named_type& entry : specification_types) {
        EXPECT_EQ(property_type_named(entry.name), entry.type) << entry.name;
        EXPECT_EQ(property_type_name(entry.type), entry.name);
        EXPECT_EQ(alternate_type_name(entry.type), entry.alternate) << entry.name;
        if (!entry.alternate.empty()) {
            EXPECT_EQ(property_type_named(entry.alternate), entry.type) << entry.alternate;
        }
    }
}

TEST(property_types, other_names_are_no_types)
{
    for (const std::string_view name :
         {"", "float32", "FLOAT32", "Float32 ", " Float32", "Table", "Float16", "Int256", "Bool", "Uint8", "Complex16",
          "Vector64", "F64vector", "Ui8Vector", "Time", "Timepoint", "string"}) {
        EXPECT_FALSE(property_type_named(name).has_value()) << name;
    }
}

TEST(property_types, categories_follow_the_tables_of_the_specification)
{
    for (const named_type& entry : specification_types) {
        const type_category category = category_of(entry.type);
        if (entry.name.ends_with("Vector")) {
            EXPECT_EQ(category, type_category::vector) << entry.name;
        } else if (entry.name.ends_with("Matrix")) {
            EXPECT_EQ(category, type_category::matrix) << entry.name;
        } else if (entry.name.starts_with("Complex")) {
            EXPECT_EQ(category, type_category::complex) << entry.name;
        } else if (entry.name == "String") {
            EXPECT_EQ(category, type_category::string);
        } else if (entry.name == "TimePoint") {
            EXPECT_EQ(category, type_category::time_point);
        } else {
            EXPECT_EQ(category, type_category::scalar) << entry.name;
        }
    }
}

TEST(property_types, vectors_and_matrices_have_the_elements_of_their_names)
{
    EXPECT_EQ(element_type(property_type::ui8_vector), property_type::uint8);
    EXPECT_EQ(element_type(property_type::i128_matrix), property_type::int128);
    EXPECT_EQ(element_type(property_type::f32_vector), property_type::float32);
    EXPECT_EQ(element_type(property_type::c64_matrix), property_type::complex64);
    EXPECT_EQ(element_type(property_type::c128_vector), property_type::complex128);
    EXPECT_EQ(element_type(property_type::float64), property_type::float64);
    // Each vector type and the matrix type of the same position have the same elements.
    for (std::size_t i = 0; i < 16; ++i) {
        const auto vector = static_cast<property_type>(static_cast<std::size_t>(property_type::i8_vector) + i);
        const auto matrix = static_cast<property_type>(static_cast<std::size_t>(property_type::i8_matrix) + i);
        EXPECT_EQ(element_type(vector), element_type(matrix)) << property_type_name(vector);
    }
}

TEST(property_types, sizes_of_values_in_data_blocks)
{
    EXPECT_EQ(value_size(property_type::boolean), 1U);
    EXPECT_EQ(value_size(property_type::uint16), 2U);
    EXPECT_EQ(value_size(property_type::float32), 4U);
    EXPECT_EQ(value_size(property_type::int64), 8U);
    EXPECT_EQ(value_size(property_type::uint128), 16U);
    EXPECT_EQ(value_size(property_type::float128), 16U);
    EXPECT_EQ(value_size(property_type::complex32), 8U);
    EXPECT_EQ(value_size(property_type::complex64), 16U);
    EXPECT_EQ(value_size(property_type::complex128), 32U);
    // Spec §10.4: the parts of a complex number are the items of the byte order.
    EXPECT_EQ(byte_order_item_size(property_type::complex32), 4U);
    EXPECT_EQ(byte_order_item_size(property_type::complex128), 16U);
    EXPECT_EQ(byte_order_item_size(property_type::int16), 2U);
}

} // namespace
