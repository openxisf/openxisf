// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <filesystem>

// Links in the file system, for the tests of path confinement.

namespace openxisf::test {

/// Creates a symbolic link at link to target, a file or a directory. False when the system does not let the process
/// create one: Windows needs a privilege or developer mode.
[[nodiscard]] bool create_symbolic_link(const std::filesystem::path& target, const std::filesystem::path& link);

#if defined(_WIN32)
/// Creates a junction at link to the directory target, which every user of Windows can create. Throws
/// std::runtime_error when it cannot.
void create_junction(const std::filesystem::path& target, const std::filesystem::path& link);
#endif

} // namespace openxisf::test
