// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/property.h>

#include <string>
#include <string_view>

// Property format specifiers (spec §8.4.3): their grammar. Rendering values with them is a separate concern.

namespace openxisf::detail {

/// Parses a format specifier: tokens name:value separated by semicolons, white space ignored everywhere. Throws
/// invalid_data_error with errc::invalid_format_specifier for an empty or unknown token, a token given twice, or a
/// value that its token does not allow.
[[nodiscard]] property_format parse_format_specifier(std::string_view text);

/// The format specifier of format: the tokens whose values differ from the defaults, in the order of spec §8.4.3. Empty
/// when every value is the default, which needs no format specifier.
[[nodiscard]] std::string format_specifier_text(const property_format& format);

} // namespace openxisf::detail
