// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "core/allocation.h"

#include <openxisf/error.h>

#include <cstddef>
#include <limits>
#include <new>
#include <string>

namespace openxisf::detail {

void check_allocation(std::uint64_t size, const limits& limits, std::string_view what)
{
    if (limits.max_allocation != 0 && size > limits.max_allocation) {
        throw limit_error(errc::allocation_too_large, std::string(what) + " " + std::to_string(size) +
                                                          " bytes, more than the allocation limit of " +
                                                          std::to_string(limits.max_allocation));
    }
    // The most that a std::vector of any type can hold, in bytes.
    if (size > static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max())) {
        throw std::bad_alloc();
    }
}

} // namespace openxisf::detail
