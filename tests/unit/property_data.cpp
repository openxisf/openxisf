// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Property values in data blocks (spec §11.1.6, §11.1.8, §11.1.9): the elements of vectors and matrices in both byte
// orders (spec §10.4), and String values as UTF-8.

#include "model/property_data.h"

#include "core/xoshiro.h"
#include "support/bytes.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <complex>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace {

using openxisf::complex128;
using openxisf::errc;
using openxisf::float128;
using openxisf::int128;
using openxisf::invalid_data_error;
using openxisf::property_type;
using openxisf::property_value;
using openxisf::uint128;
using openxisf::usage_error;
using openxisf::detail::byte_order;
using openxisf::detail::decode_matrix;
using openxisf::detail::decode_string;
using openxisf::detail::decode_vector;
using openxisf::detail::elements_size;
using openxisf::detail::encode_elements;
using openxisf::test::throws;

std::vector<std::byte> data(std::initializer_list<unsigned int> values)
{
    std::vector<std::byte> result;
    for (const unsigned int value : values) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

// The bytes of the elements of value in order, and back to the same value.
void check_both_ways(const property_value& value, byte_order order, const std::vector<std::byte>& expected)
{
    EXPECT_EQ(encode_elements(value, order), expected);
    if (value.rows() == 0) {
        EXPECT_EQ(decode_vector(value.type(), expected, order), value);
    } else {
        EXPECT_EQ(decode_matrix(value.type(), value.rows(), value.columns(), expected, order), value);
    }
}

TEST(property_data, integers_in_both_byte_orders)
{
    const property_value u16(std::vector<std::uint16_t>{0x0102, 0xA0B0});
    check_both_ways(u16, byte_order::little, data({0x02, 0x01, 0xB0, 0xA0}));
    check_both_ways(u16, byte_order::big, data({0x01, 0x02, 0xA0, 0xB0}));
    const property_value i32(std::vector<std::int32_t>{-2});
    check_both_ways(i32, byte_order::little, data({0xFE, 0xFF, 0xFF, 0xFF}));
    check_both_ways(i32, byte_order::big, data({0xFF, 0xFF, 0xFF, 0xFE}));
    const property_value u64(std::vector<std::uint64_t>{0x0102030405060708});
    check_both_ways(u64, byte_order::little, data({8, 7, 6, 5, 4, 3, 2, 1}));
    check_both_ways(u64, byte_order::big, data({1, 2, 3, 4, 5, 6, 7, 8}));
    // Bytes have no byte order.
    const property_value i8(std::vector<std::int8_t>{-1, 2});
    check_both_ways(i8, byte_order::little, data({0xFF, 0x02}));
    check_both_ways(i8, byte_order::big, data({0xFF, 0x02}));
}

TEST(property_data, floating_point_and_complex_numbers_swap_each_part)
{
    const property_value f32(std::vector<float>{1.0F});
    check_both_ways(f32, byte_order::little, data({0x00, 0x00, 0x80, 0x3F}));
    check_both_ways(f32, byte_order::big, data({0x3F, 0x80, 0x00, 0x00}));
    // Spec §8.4.4.2: the real part first; each part in the byte order of the block.
    const property_value c32(std::vector<std::complex<float>>{{1.0F, 2.0F}});
    check_both_ways(c32, byte_order::little, data({0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x40}));
    check_both_ways(c32, byte_order::big, data({0x3F, 0x80, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00}));
    const property_value c64(std::vector<std::complex<double>>{{1.0, -2.0}});
    check_both_ways(c64, byte_order::big, data({0x3F, 0xF0, 0, 0, 0, 0, 0, 0, 0xC0, 0x00, 0, 0, 0, 0, 0, 0}));
}

TEST(property_data, matrices_are_stored_in_row_order)
{
    // Spec §8.4.4.6: the elements of the first row, then those of the second.
    const property_value matrix = property_value::matrix(2, 3, std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6});
    check_both_ways(matrix, byte_order::little, data({1, 2, 3, 4, 5, 6}));
    const property_value doubles = property_value::matrix(2, 1, std::vector<double>{1.0, -2.0});
    check_both_ways(doubles, byte_order::little, data({0, 0, 0, 0, 0, 0, 0xF0, 0x3F, 0, 0, 0, 0, 0, 0, 0x00, 0xC0}));
}

TEST(property_data, values_of_128_bits)
{
    const property_value i128(std::vector<int128>{{.high = -1, .low = 0xFFFF'FFFF'FFFF'FFFE}});
    check_both_ways(
        i128, byte_order::little,
        data({0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
    check_both_ways(
        i128, byte_order::big,
        data({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE}));
    const property_value u128(std::vector<uint128>{{.high = 1, .low = 2}});
    check_both_ways(u128, byte_order::little, data({2, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}));
    check_both_ways(u128, byte_order::big, data({0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 2}));
    // 1.0 in binary128.
    const float128 one{.high = 0x3FFF'0000'0000'0000, .low = 0};
    check_both_ways(property_value(std::vector<float128>{one}), byte_order::little,
                    data({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0x3F}));
    check_both_ways(property_value(std::vector<float128>{one}), byte_order::big,
                    data({0x3F, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
    const float128 two{.high = 0x4000'0000'0000'0000, .low = 0};
    check_both_ways(property_value(std::vector<complex128>{{.real = one, .imag = two}}), byte_order::big,
                    data({0x3F, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                          0x40, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
}

template <typename T> void check_round_trip(openxisf::detail::xoshiro256starstar& random)
{
    std::vector<std::byte> bytes(17 * sizeof(T));
    for (std::byte& b : bytes) {
        b = static_cast<std::byte>(random() & 0xFFU);
    }
    for (const byte_order order : {byte_order::little, byte_order::big}) {
        const property_value vector = decode_vector(property_value(std::vector<T>{}).type(), bytes, order);
        EXPECT_EQ(vector.length(), 17U);
        EXPECT_EQ(encode_elements(vector, order), bytes);
        const property_value matrix =
            decode_matrix(property_value::matrix(0, 0, std::vector<T>{}).type(), 17, 1, bytes, order);
        EXPECT_EQ(encode_elements(matrix, order), bytes);
    }
}

TEST(property_data, every_element_type_gives_back_its_bytes)
{
    openxisf::detail::xoshiro256starstar random({1, 2, 3, 4});
    check_round_trip<std::int8_t>(random);
    check_round_trip<std::uint8_t>(random);
    check_round_trip<std::int16_t>(random);
    check_round_trip<std::uint16_t>(random);
    check_round_trip<std::int32_t>(random);
    check_round_trip<std::uint32_t>(random);
    check_round_trip<std::int64_t>(random);
    check_round_trip<std::uint64_t>(random);
    check_round_trip<int128>(random);
    check_round_trip<uint128>(random);
    check_round_trip<float>(random);
    check_round_trip<double>(random);
    check_round_trip<float128>(random);
    check_round_trip<std::complex<float>>(random);
    check_round_trip<std::complex<double>>(random);
    check_round_trip<complex128>(random);
}

TEST(property_data, empty_vectors_and_matrices)
{
    EXPECT_EQ(decode_vector(property_type::f64_vector, {}, byte_order::little), property_value(std::vector<double>{}));
    EXPECT_EQ(decode_matrix(property_type::f64_matrix, 0, 0, {}, byte_order::big),
              property_value::matrix(0, 0, std::vector<double>{}));
    EXPECT_EQ(decode_matrix(property_type::ui16_matrix, 3, 0, {}, byte_order::little).rows(), 3U);
    EXPECT_TRUE(encode_elements(property_value(std::vector<double>{}), byte_order::little).empty());
}

TEST(property_data, the_data_must_have_the_size_of_the_elements)
{
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [] {
        (void)decode_vector(property_type::ui16_vector, data({1, 2, 3}), byte_order::little);
    }));
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [] {
        (void)decode_matrix(property_type::ui8_matrix, 2, 2, data({1, 2, 3}), byte_order::little);
    }));
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [] {
        (void)decode_matrix(property_type::ui8_matrix, std::uint64_t{1} << 32U, std::uint64_t{1} << 32U, {},
                            byte_order::little);
    }));
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument,
                                    [] { (void)decode_vector(property_type::string, {}, byte_order::little); }));
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument,
                                    [] { (void)encode_elements(property_value(1.0), byte_order::little); }));
}

TEST(property_data, sizes_of_elements)
{
    EXPECT_EQ(elements_size(property_type::f64_vector, 3), 24U);
    EXPECT_EQ(elements_size(property_type::c128_matrix, 2), 64U);
    EXPECT_EQ(elements_size(property_type::ui8_vector, std::numeric_limits<std::uint64_t>::max()),
              std::numeric_limits<std::uint64_t>::max());
    EXPECT_FALSE(elements_size(property_type::ui16_vector, std::uint64_t{1} << 63U).has_value());
    EXPECT_FALSE(elements_size(property_type::string, 1).has_value());
}

TEST(property_data, strings_are_utf8_without_nul)
{
    EXPECT_EQ(decode_string(openxisf::test::bytes("  Ñandú — 星雲 ✓  ")), "  Ñandú — 星雲 ✓  ");
    EXPECT_EQ(decode_string({}), "");
    EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_utf8, [] { (void)decode_string(data({'a', 0x00, 'b'})); }));
    EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_utf8, [] { (void)decode_string(data({0xC3, 0x28})); }));
    EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_utf8, [] { (void)decode_string(data({0xED, 0xA0, 0x80})); }));
}

} // namespace
