// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#pragma once

#include <string_view>

namespace openxisf::detail {

/// True when text is well-formed UTF-8 (Unicode §3.9, table 3-7) and does not contain U+0000. Overlong forms,
/// surrogates and code points above U+10FFFF are rejected.
///
/// XISF never allows U+0000: not in strings (spec §8.4.4.3), not in the XML header, and not in paths.
[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept;

} // namespace openxisf::detail
