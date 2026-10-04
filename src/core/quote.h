// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <string>
#include <string_view>

namespace openxisf::detail {

/// Untrusted text quoted for an error message: in single quotes, printable ASCII kept, every other byte written as
/// \xHH, and cut after 40 bytes. The message stays short and readable whatever the input holds.
[[nodiscard]] std::string quote(std::string_view text);

} // namespace openxisf::detail
