// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "model/property_data.h"

#include <openxisf/error.h>

#include "core/endian.h"
#include "core/utf8.h"
#include "model/property_types.h"

#include <bit>
#include <complex>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace openxisf::detail {

namespace {

static_assert(std::is_trivially_copyable_v<std::complex<float>> && sizeof(std::complex<float>) == 8);
static_assert(std::is_trivially_copyable_v<std::complex<double>> && sizeof(std::complex<double>) == 16);

// Calls function with std::type_identity of the C++ type of the elements of a vector or matrix type.
template <typename Function> decltype(auto) with_element_type(property_type type, Function&& function)
{
    switch (element_type(type)) {
    case property_type::int8:
        return std::forward<Function>(function)(std::type_identity<std::int8_t>{});
    case property_type::uint8:
        return std::forward<Function>(function)(std::type_identity<std::uint8_t>{});
    case property_type::int16:
        return std::forward<Function>(function)(std::type_identity<std::int16_t>{});
    case property_type::uint16:
        return std::forward<Function>(function)(std::type_identity<std::uint16_t>{});
    case property_type::int32:
        return std::forward<Function>(function)(std::type_identity<std::int32_t>{});
    case property_type::uint32:
        return std::forward<Function>(function)(std::type_identity<std::uint32_t>{});
    case property_type::int64:
        return std::forward<Function>(function)(std::type_identity<std::int64_t>{});
    case property_type::uint64:
        return std::forward<Function>(function)(std::type_identity<std::uint64_t>{});
    case property_type::int128:
        return std::forward<Function>(function)(std::type_identity<int128>{});
    case property_type::uint128:
        return std::forward<Function>(function)(std::type_identity<uint128>{});
    case property_type::float32:
        return std::forward<Function>(function)(std::type_identity<float>{});
    case property_type::float64:
        return std::forward<Function>(function)(std::type_identity<double>{});
    case property_type::float128:
        return std::forward<Function>(function)(std::type_identity<float128>{});
    case property_type::complex32:
        return std::forward<Function>(function)(std::type_identity<std::complex<float>>{});
    case property_type::complex64:
        return std::forward<Function>(function)(std::type_identity<std::complex<double>>{});
    case property_type::complex128:
        return std::forward<Function>(function)(std::type_identity<complex128>{});
    default:
        break;
    }
    throw usage_error(errc::invalid_argument,
                      "a " + std::string(property_type_name(type)) + " value is neither a vector nor a matrix");
}

bool is_swapped(byte_order order) noexcept
{
    return (order == byte_order::big) != (std::endian::native == std::endian::big);
}

// A 128-bit item: its two halves, in either byte order.
struct halves
{
    std::uint64_t high = 0;
    std::uint64_t low = 0;
};

halves load_halves(std::span<const std::byte, 16> bytes, byte_order order) noexcept
{
    if (order == byte_order::little) {
        return {.high = load_little_endian<std::uint64_t>(bytes.subspan<8, 8>()),
                .low = load_little_endian<std::uint64_t>(bytes.first<8>())};
    }
    return {.high = load_big_endian<std::uint64_t>(bytes.first<8>()),
            .low = load_big_endian<std::uint64_t>(bytes.subspan<8, 8>())};
}

void store_halves(std::span<std::byte, 16> bytes, halves value, byte_order order) noexcept
{
    if (order == byte_order::little) {
        store_little_endian(bytes.first<8>(), value.low);
        store_little_endian(bytes.subspan<8, 8>(), value.high);
    } else {
        store_big_endian(bytes.first<8>(), value.high);
        store_big_endian(bytes.subspan<8, 8>(), value.low);
    }
}

template <typename T> T load_element(std::span<const std::byte, 16> bytes, byte_order order) noexcept
{
    const halves value = load_halves(bytes, order);
    if constexpr (std::same_as<T, int128>) {
        return {.high = static_cast<std::int64_t>(value.high), .low = value.low};
    } else {
        return {.high = value.high, .low = value.low};
    }
}

template <typename T> halves halves_of(const T& value) noexcept
{
    if constexpr (std::same_as<T, int128>) {
        return {.high = static_cast<std::uint64_t>(value.high), .low = value.low};
    } else {
        return {.high = value.high, .low = value.low};
    }
}

template <typename T> std::vector<T> decode_elements(std::span<const std::byte> data, byte_order order)
{
    std::vector<T> elements(data.size() / sizeof(T));
    if constexpr (std::same_as<T, int128> || std::same_as<T, uint128> || std::same_as<T, float128>) {
        for (std::size_t i = 0; i < elements.size(); ++i) {
            elements[i] = load_element<T>(data.subspan(i * 16).first<16>(), order);
        }
    } else if constexpr (std::same_as<T, complex128>) {
        for (std::size_t i = 0; i < elements.size(); ++i) {
            const std::span<const std::byte> item = data.subspan(i * 32);
            elements[i] = {.real = load_element<float128>(item.first<16>(), order),
                           .imag = load_element<float128>(item.subspan(16).first<16>(), order)};
        }
    } else if (!elements.empty()) {
        std::memcpy(elements.data(), data.data(), elements.size() * sizeof(T));
        if (is_swapped(order)) {
            // The scalars of a complex number are swapped one by one.
            if constexpr (std::same_as<T, std::complex<float>> || std::same_as<T, std::complex<double>>) {
                swap_byte_order(std::as_writable_bytes(std::span(elements)), sizeof(T) / 2);
            } else {
                swap_byte_order(std::as_writable_bytes(std::span(elements)), sizeof(T));
            }
        }
    }
    return elements;
}

template <typename T> std::vector<std::byte> encode(std::span<const T> elements, byte_order order)
{
    std::vector<std::byte> data(elements.size() * sizeof(T));
    const std::span<std::byte> bytes(data);
    if constexpr (std::same_as<T, int128> || std::same_as<T, uint128> || std::same_as<T, float128>) {
        for (std::size_t i = 0; i < elements.size(); ++i) {
            store_halves(bytes.subspan(i * 16).first<16>(), halves_of(elements[i]), order);
        }
    } else if constexpr (std::same_as<T, complex128>) {
        for (std::size_t i = 0; i < elements.size(); ++i) {
            const std::span<std::byte> item = bytes.subspan(i * 32);
            store_halves(item.first<16>(), halves_of(elements[i].real), order);
            store_halves(item.subspan(16).first<16>(), halves_of(elements[i].imag), order);
        }
    } else if (!data.empty()) {
        std::memcpy(data.data(), elements.data(), data.size());
        if (is_swapped(order)) {
            if constexpr (std::same_as<T, std::complex<float>> || std::same_as<T, std::complex<double>>) {
                swap_byte_order(data, sizeof(T) / 2);
            } else {
                swap_byte_order(data, sizeof(T));
            }
        }
    }
    return data;
}

} // namespace

std::optional<std::uint64_t> elements_size(property_type type, std::uint64_t count) noexcept
{
    const std::uint64_t size = value_size(element_type(type));
    if (size == 0 || count > std::numeric_limits<std::uint64_t>::max() / size) {
        return std::nullopt;
    }
    return count * size;
}

property_value decode_vector(property_type type, std::span<const std::byte> data, byte_order order)
{
    if (category_of(type) != type_category::vector || data.size() % value_size(element_type(type)) != 0) {
        throw usage_error(errc::invalid_argument,
                          "the data are not those of a " + std::string(property_type_name(type)) + " value");
    }
    return with_element_type(
        type, [&]<typename T>(std::type_identity<T>) { return property_value(decode_elements<T>(data, order)); });
}

property_value decode_matrix(property_type type, std::uint64_t rows, std::uint64_t columns,
                             std::span<const std::byte> data, byte_order order)
{
    const bool fits = rows == 0 || columns <= std::numeric_limits<std::uint64_t>::max() / rows;
    if (category_of(type) != type_category::matrix || !fits || elements_size(type, rows * columns) != data.size()) {
        throw usage_error(errc::invalid_argument, "the data are not those of a " +
                                                      std::string(property_type_name(type)) + " value of " +
                                                      std::to_string(rows) + " by " + std::to_string(columns));
    }
    return with_element_type(type, [&]<typename T>(std::type_identity<T>) {
        return property_value::matrix(rows, columns, decode_elements<T>(data, order));
    });
}

std::string decode_string(std::span<const std::byte> data)
{
    std::string text(data.size(), '\0');
    if (!data.empty()) {
        std::memcpy(text.data(), data.data(), data.size());
    }
    if (!is_valid_utf8(text)) {
        throw invalid_data_error(errc::invalid_utf8, "the String value is not valid UTF-8, or contains U+0000");
    }
    return text;
}

std::vector<std::byte> encode_elements(const property_value& value, byte_order order)
{
    const type_category category = category_of(value.type());
    if (category != type_category::vector && category != type_category::matrix) {
        throw usage_error(errc::invalid_argument,
                          "a " + std::string(property_type_name(value.type())) + " value has no elements");
    }
    return with_element_type(value.type(),
                             [&]<typename T>(std::type_identity<T>) { return encode<T>(value.elements<T>(), order); });
}

} // namespace openxisf::detail
