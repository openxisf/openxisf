// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/limits.h>

#include <cstdint>
#include <string_view>

namespace openxisf::detail {

/// Checks an allocation of size bytes in one piece, whose size a unit gives: limit_error with
/// errc::allocation_too_large above limits.max_allocation, and std::bad_alloc beyond what any allocation can hold,
/// which no limit (0) would otherwise leave to std::vector, as std::length_error. what describes the data in the
/// message, as in "the data block decompresses to".
void check_allocation(std::uint64_t size, const limits& limits, std::string_view what);

} // namespace openxisf::detail
