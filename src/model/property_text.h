// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/property.h>

#include "core/text_grammar.h"

#include <complex>
#include <string>
#include <string_view>

// Properties as text: identifiers (spec §8.4.1), and the value attributes of scalar, complex and TimePoint properties
// (spec §11.1.4, §11.1.5, §11.1.7). Parsers throw invalid_data_error, whose message has no context; the caller adds it.

namespace openxisf::detail {

/// True when id is a property identifier: [_a-zA-Z][_a-zA-Z0-9]*(:[_a-zA-Z][_a-zA-Z0-9]*)*.
[[nodiscard]] bool is_property_id(std::string_view id) noexcept;

/// Parses a complex number, (real,imag), with white space ignored around each part. Throws invalid_data_error with
/// errc::invalid_complex for the form, and the errors of parse_float() for the parts.
template <xisf_float T> [[nodiscard]] std::complex<T> parse_complex(std::string_view text);

/// Checks a Complex128 value, (real,imag) with Float128 parts, and returns it without white space around it and its
/// parts (spec §8.3.5). Complex128 values are kept as text.
[[nodiscard]] std::string check_complex128(std::string_view text);

/// (real,imag), each part as format_float() writes it.
template <xisf_float T> [[nodiscard]] std::string format_complex(std::complex<T> value);

/// The value of the value attribute of a property of a scalar, complex or TimePoint type. Throws invalid_data_error
/// with the code of the grammar of the type, or with errc::value_out_of_range; and usage_error for any other type.
[[nodiscard]] property_value parse_value_attribute(property_type type, std::string_view text);

/// The text of the value attribute of a scalar, complex or TimePoint value, which parses back to it. Float128 and
/// Complex128 values keep their text. Throws usage_error for any other value, and for a TimePoint that is not valid.
[[nodiscard]] std::string format_value_attribute(const property_value& value);

} // namespace openxisf::detail
