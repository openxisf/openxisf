// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "core/xoshiro.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

// Universally unique identifiers (RFC 9562), as the uuid attribute of the Image element uses them (spec §11.5.2).

namespace openxisf::detail {

/// The 16 bytes of a UUID, in the order of its canonical form.
using uuid = std::array<std::byte, 16>;

/// A version 4 UUID: 122 bits from the generator, with the version and variant fields set.
[[nodiscard]] uuid make_uuid_v4(xoshiro256starstar& generator) noexcept;

/// The canonical form, such as c5c93b6d-9072-4e85-9548-1a5391377683, in lowercase.
[[nodiscard]] std::string format_uuid(const uuid& id);

/// True when id is a version 4 UUID of the variant of RFC 9562.
[[nodiscard]] bool is_version_4_uuid(const uuid& id) noexcept;

/// Parses the canonical form. Hexadecimal digits are accepted in either case, as RFC 9562 requires. Throws
/// invalid_data_error.
[[nodiscard]] uuid parse_uuid(std::string_view text);

} // namespace openxisf::detail
