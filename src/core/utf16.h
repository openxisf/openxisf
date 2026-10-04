// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <string>
#include <string_view>

// Conversions for the wide-character functions of Windows. They are portable code, so they are tested everywhere.

namespace openxisf::detail {

/// The UTF-16 form of UTF-8 text. Throws invalid_data_error (errc::invalid_utf8) unless is_valid_utf8(text).
[[nodiscard]] std::u16string utf8_to_utf16(std::string_view text);

/// The UTF-8 form of UTF-16 text. Throws invalid_data_error (errc::invalid_utf16) for an unpaired surrogate or U+0000,
/// which UTF-8 text never holds either.
[[nodiscard]] std::string utf16_to_utf8(std::u16string_view text);

} // namespace openxisf::detail
