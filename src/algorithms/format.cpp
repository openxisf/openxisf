// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <openxisf/error.h>
#include <openxisf/format.h>

#include "core/text_grammar.h"
#include "core/utc_time.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

namespace openxisf {

namespace {

// The largest width and precision applied, so that a format read from a file cannot ask for gigabytes.
constexpr std::uint32_t largest = 1024;

template <typename T> struct vector_of : std::false_type
{};

template <typename T> struct vector_of<std::vector<T>> : std::true_type
{};

// The number of characters of UTF-8 text: its bytes that do not continue a sequence.
std::size_t character_count(std::string_view text) noexcept
{
    return static_cast<std::size_t>(
        std::ranges::count_if(text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
}

// text padded to the width of the format, with its fill character and alignment.
std::string pad(std::string text, const property_format& format)
{
    const std::size_t width = std::min(format.width, largest);
    const std::size_t length = character_count(text);
    if (length >= width) {
        return text;
    }
    const std::size_t padding = width - length;
    switch (format.align) {
    case format_align::left:
        text.append(padding, format.fill);
        return text;
    case format_align::center:
        // An odd padding puts one more character before the value than after it.
        return std::string((padding + 1) / 2, format.fill) + text + std::string(padding / 2, format.fill);
    case format_align::right:
        break;
    }
    return std::string(padding, format.fill) + text;
}

// A number from the text of its magnitude: a sign as the format says, and none for a value represented as zero.
std::string signed_number(bool negative, std::string magnitude, bool represented_as_zero, const property_format& format)
{
    if (!represented_as_zero) {
        if (negative) {
            magnitude.insert(magnitude.begin(), '-');
        } else if (format.sign == format_sign::force) {
            magnitude.insert(magnitude.begin(), '+');
        }
    }
    return pad(std::move(magnitude), format);
}

unsigned radix_of(format_base base) noexcept
{
    switch (base) {
    case format_base::binary:
        return 2;
    case format_base::octal:
        return 8;
    case format_base::hexadecimal:
        return 16;
    case format_base::decimal:
        break;
    }
    return 10;
}

// The digits of high × 2^64 + low in a radix of at most 16, by long division of its 32-bit halves.
std::string digits_of(std::uint64_t high, std::uint64_t low, unsigned radix)
{
    std::array<std::uint32_t, 4> parts{static_cast<std::uint32_t>(high >> 32U), static_cast<std::uint32_t>(high),
                                       static_cast<std::uint32_t>(low >> 32U), static_cast<std::uint32_t>(low)};
    std::string result;
    do {
        std::uint64_t remainder = 0;
        for (std::uint32_t& part : parts) {
            const std::uint64_t current = (remainder << 32U) | part;
            part = static_cast<std::uint32_t>(current / radix);
            remainder = current % radix;
        }
        result.push_back("0123456789abcdef"[remainder]);
    } while (std::ranges::any_of(parts, [](std::uint32_t part) { return part != 0; }));
    std::ranges::reverse(result);
    return result;
}

std::string integer_text(bool negative, std::uint64_t high, std::uint64_t low, const property_format& format)
{
    return signed_number(negative, digits_of(high, low, radix_of(format.base)), high == 0 && low == 0, format);
}

std::string floating_text(double value, const property_format& format)
{
    if (std::isnan(value)) {
        return pad("nan", format);
    }
    const bool negative = std::signbit(value);
    if (std::isinf(value)) {
        return signed_number(negative, "inf", false, format);
    }
    std::chars_format notation = std::chars_format::general;
    if (format.notation == format_notation::fixed) {
        notation = std::chars_format::fixed;
    } else if (format.notation == format_notation::scientific) {
        notation = std::chars_format::scientific;
    }
    // The longest text: the 309 digits of the integer part of the largest double, a point and the largest precision.
    std::array<char, 309 + 1 + largest + 16> text{};
    const std::to_chars_result result = std::to_chars(text.data(), text.data() + text.size(), std::abs(value), notation,
                                                      static_cast<int>(std::min(format.precision, largest)));
    if (result.ec != std::errc{}) {
        throw usage_error(errc::invalid_argument, "a floating point value does not fit its representation");
    }
    std::string magnitude(text.data(), result.ptr);
    // Represented as zero when the digits before the exponent are all zeros (spec §8.4.3).
    const std::string_view significand = std::string_view(magnitude).substr(0, magnitude.find('e'));
    const bool zero = std::ranges::none_of(significand, [](char c) { return c >= '1' && c <= '9'; });
    return signed_number(negative, std::move(magnitude), zero, format);
}

// The double nearest to a Float128 value kept as text, an infinity beyond the range of double.
double nearest_double(std::string_view text)
{
    try {
        return detail::parse_float<double>(text);
    } catch (const invalid_data_error&) {
        const bool negative = detail::trim_white_space(text).starts_with('-');
        return negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    }
}

std::string complex_text(double real, double imag, const property_format& format)
{
    return '(' + floating_text(real, format) + ',' + floating_text(imag, format) + ')';
}

// A Complex128 value kept as text: (real,imag), which the value was checked to be.
std::string complex128_text(std::string_view text, const property_format& format)
{
    const std::string_view value = detail::trim_white_space(text);
    const std::size_t comma = value.find(',');
    return complex_text(nearest_double(value.substr(1, comma - 1)),
                        nearest_double(value.substr(comma + 1, value.size() - comma - 2)), format);
}

// A TimePoint in ISO 8601, in UTC, as the writer writes it, and also with the years -1 and 10000 that a time with an
// offset from UTC can reach (spec §8.4.4.4). The rest of such a time is checked on a year of the same length.
std::string time_text(const date_time& time)
{
    if (detail::is_valid_time_point(time)) {
        return detail::format_time_point(time);
    }
    date_time same = time;
    same.year = time.year == -1 ? 2001 : 2000;
    if ((time.year != -1 && time.year != 10000) || !detail::is_valid_time_point(same)) {
        throw usage_error(errc::invalid_argument, "the TimePoint value is not a valid date and time");
    }
    return (time.year == -1 ? "-0001" : "10000") + detail::format_time_point(same).substr(4);
}

// A scalar, or an element of a vector or a matrix.
template <typename T> std::string element_text(const T& element, const property_format& format)
{
    if constexpr (std::is_same_v<T, bool>) {
        if (format.boolean == format_bool::numeric) {
            return pad(element ? "1" : "0", format);
        }
        return pad(element ? "true" : "false", format);
    } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
        // The magnitude of a negative value in two's complement, which holds that of the smallest one.
        // NOLINTNEXTLINE(bugprone-signed-char-misuse,cert-str34-c): an Int8 value is a number, not a character.
        const std::int64_t wide = element;
        const auto bits = static_cast<std::uint64_t>(wide);
        return integer_text(element < 0, 0, element < 0 ? 0 - bits : bits, format);
    } else if constexpr (std::is_integral_v<T>) {
        return integer_text(false, 0, element, format);
    } else if constexpr (std::is_same_v<T, int128>) {
        const auto high = static_cast<std::uint64_t>(element.high);
        if (element.high >= 0) {
            return integer_text(false, high, element.low, format);
        }
        // The two's complement of both halves, the carry of the low half going into the high one.
        const std::uint64_t low = 0 - element.low;
        return integer_text(true, ~high + (low == 0 ? 1U : 0U), low, format);
    } else if constexpr (std::is_same_v<T, uint128>) {
        return integer_text(false, element.high, element.low, format);
    } else if constexpr (std::is_same_v<T, double>) {
        return floating_text(element, format);
    } else if constexpr (std::is_same_v<T, float>) {
        return floating_text(static_cast<double>(element), format);
    } else if constexpr (std::is_same_v<T, float128>) {
        return floating_text(to_double(element), format);
    } else if constexpr (std::is_same_v<T, complex128>) {
        return complex_text(to_double(element.real), to_double(element.imag), format);
    } else if constexpr (std::is_same_v<T, std::complex<double>>) {
        return complex_text(element.real(), element.imag(), format);
    } else {
        return complex_text(static_cast<double>(element.real()), static_cast<double>(element.imag()), format);
    }
}

} // namespace

std::string format_value(const property_value& value, const property_format& format)
{
    return std::visit(
        [&]<typename T>(const T& held) -> std::string {
            if constexpr (std::is_same_v<T, std::string>) {
                if (value.type() == property_type::float128) {
                    return floating_text(nearest_double(held), format);
                }
                if (value.type() == property_type::complex128) {
                    return complex128_text(held, format);
                }
                return pad(held, format);
            } else if constexpr (std::is_same_v<T, date_time>) {
                // Spec §8.4.3.1: no format applies.
                return time_text(held);
            } else if constexpr (vector_of<T>::value) {
                // A vector is one row of a matrix.
                const std::uint64_t columns = value.columns() != 0 ? value.columns() : held.size();
                std::string text;
                for (std::size_t i = 0; i < held.size(); ++i) {
                    if (i != 0) {
                        text += i % columns == 0 ? ';' : ',';
                    }
                    text += element_text(held[i], format);
                }
                return text;
            } else {
                return element_text(held, format);
            }
        },
        value.data());
}

std::string format_element(const property_value& value, std::uint64_t index, const property_format& format)
{
    return std::visit(
        [&]<typename T>(const T& held) -> std::string {
            if constexpr (vector_of<T>::value) {
                if (index < held.size()) {
                    return element_text(held[index], format);
                }
                throw usage_error(errc::invalid_argument, "the index of an element is beyond the end of the " +
                                                              std::string(property_type_name(value.type())) + " value");
            } else {
                throw usage_error(errc::invalid_argument,
                                  "a " + std::string(property_type_name(value.type())) + " value has no elements");
            }
        },
        value.data());
}

} // namespace openxisf
