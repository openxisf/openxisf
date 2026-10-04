// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <openxisf/version.h>

namespace openxisf {

std::string_view version() noexcept
{
    return OPENXISF_VERSION_STRING;
}

} // namespace openxisf
