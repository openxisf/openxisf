// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "model/property_text.h"

#include <openxisf/error.h>

#include "core/quote.h"
#include "core/utc_time.h"
#include "model/property_types.h"

#include <cstdint>
#include <utility>

namespace openxisf::detail {

namespace {

bool is_identifier_start(char c) noexcept
{
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_identifier_character(char c) noexcept
{
    return is_identifier_start(c) || (c >= '0' && c <= '9');
}

// The real and imaginary parts of a complex number, as written between its parentheses.
struct complex_parts
{
    std::string_view real{};
    std::string_view imag{};
};

complex_parts split_complex(std::string_view text)
{
    const std::string_view value = trim_white_space(text);
    const std::size_t comma = value.find(',');
    if (value.size() < 2 || value.front() != '(' || value.back() != ')' || comma == std::string_view::npos ||
        value.find(',', comma + 1) != std::string_view::npos) {
        throw invalid_data_error(errc::invalid_complex, quote(text) + " is not a complex number");
    }
    return {.real = value.substr(1, comma - 1), .imag = value.substr(comma + 1, value.size() - comma - 2)};
}

template <xisf_float T> std::string format_complex_value(const property_value& value)
{
    return format_complex(value.get<std::complex<T>>());
}

} // namespace

bool is_property_id(std::string_view id) noexcept
{
    // Colons separate identifiers, each of at least one character.
    bool at_start = true;
    for (const char c : id) {
        if (c == ':') {
            if (at_start) {
                return false;
            }
            at_start = true;
        } else if (at_start ? is_identifier_start(c) : is_identifier_character(c)) {
            at_start = false;
        } else {
            return false;
        }
    }
    return !at_start;
}

template <xisf_float T> std::complex<T> parse_complex(std::string_view text)
{
    const complex_parts parts = split_complex(text);
    return {parse_float<T>(parts.real), parse_float<T>(parts.imag)};
}

std::string_view check_complex128(std::string_view text)
{
    const complex_parts parts = split_complex(text);
    (void)check_float128(parts.real);
    (void)check_float128(parts.imag);
    return trim_white_space(text);
}

template <xisf_float T> std::string format_complex(std::complex<T> value)
{
    return '(' + format_float(value.real()) + ',' + format_float(value.imag()) + ')';
}

property_value parse_value_attribute(property_type type, std::string_view text)
{
    switch (type) {
    case property_type::boolean:
        return parse_boolean(text);
    case property_type::int8:
        return parse_integer<std::int8_t>(text);
    case property_type::uint8:
        return parse_integer<std::uint8_t>(text);
    case property_type::int16:
        return parse_integer<std::int16_t>(text);
    case property_type::uint16:
        return parse_integer<std::uint16_t>(text);
    case property_type::int32:
        return parse_integer<std::int32_t>(text);
    case property_type::uint32:
        return parse_integer<std::uint32_t>(text);
    case property_type::int64:
        return parse_integer<std::int64_t>(text);
    case property_type::uint64:
        return parse_integer<std::uint64_t>(text);
    case property_type::int128:
        return parse_integer<int128>(text);
    case property_type::uint128:
        return parse_integer<uint128>(text);
    case property_type::float32:
        return parse_float<float>(text);
    case property_type::float64:
        return parse_float<double>(text);
    case property_type::float128:
        return property_value::from_float128_text(std::string(check_float128(text)));
    case property_type::complex32:
        return parse_complex<float>(text);
    case property_type::complex64:
        return parse_complex<double>(text);
    case property_type::complex128:
        return property_value::from_complex128_text(std::string(check_complex128(text)));
    case property_type::time_point:
        return parse_time_point(text);
    default:
        break;
    }
    throw usage_error(errc::invalid_argument,
                      "a " + std::string(property_type_name(type)) + " value is not written in a value attribute");
}

std::string format_value_attribute(const property_value& value)
{
    switch (value.type()) {
    case property_type::boolean:
        return std::string(format_boolean(value.get<bool>()));
    case property_type::int8:
        return format_integer(value.get<std::int8_t>());
    case property_type::uint8:
        return format_integer(value.get<std::uint8_t>());
    case property_type::int16:
        return format_integer(value.get<std::int16_t>());
    case property_type::uint16:
        return format_integer(value.get<std::uint16_t>());
    case property_type::int32:
        return format_integer(value.get<std::int32_t>());
    case property_type::uint32:
        return format_integer(value.get<std::uint32_t>());
    case property_type::int64:
        return format_integer(value.get<std::int64_t>());
    case property_type::uint64:
        return format_integer(value.get<std::uint64_t>());
    case property_type::int128:
        return format_integer(value.get<int128>());
    case property_type::uint128:
        return format_integer(value.get<uint128>());
    case property_type::float32:
        return format_float(value.get<float>());
    case property_type::float64:
        return format_float(value.get<double>());
    case property_type::float128:
    case property_type::complex128:
        return value.get<std::string>();
    case property_type::complex32:
        return format_complex_value<float>(value);
    case property_type::complex64:
        return format_complex_value<double>(value);
    case property_type::time_point:
        if (!is_valid_time_point(value.get<date_time>())) {
            throw usage_error(errc::invalid_argument, "the TimePoint value is not a valid date and time");
        }
        return format_time_point(value.get<date_time>());
    default:
        break;
    }
    throw usage_error(errc::invalid_argument, "a " + std::string(property_type_name(value.type())) +
                                                  " value is not written in a value attribute");
}

template std::complex<float> parse_complex<float>(std::string_view);
template std::complex<double> parse_complex<double>(std::string_view);
template std::string format_complex<float>(std::complex<float>);
template std::string format_complex<double>(std::complex<double>);

} // namespace openxisf::detail
