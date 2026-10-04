// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <string>
#include <string_view>

// Paths of local files, for the resolver of external data blocks. A unit writes them in UNIX syntax (spec §10.3); the
// system takes them in its own. Every path is UTF-8.

namespace openxisf::detail {

/// The directory of the file at path, in the syntax of the system: "." for a file name alone, and a root with its
/// separator, such as "/" or, on Windows, "C:\".
[[nodiscard]] std::string parent_directory(std::string_view path);

/// The name of the file at path, in the syntax of the system: what follows its directory.
[[nodiscard]] std::string_view file_name(std::string_view path) noexcept;

/// The path of the system for relative, a path in UNIX syntax from directory. On Windows, throws unsupported_error with
/// errc::unsupported_location when relative holds a character that no file name can hold there, such as a backslash or
/// a colon.
[[nodiscard]] std::string relative_system_path(std::string_view directory, std::string_view relative);

/// The path of the system for absolute, an absolute path in UNIX syntax: absolute itself, except on Windows, where
/// /c/dir/file names C:/dir/file. There, throws unsupported_error with errc::location_not_allowed for a network path,
/// which starts with two slashes, and with errc::unsupported_location for any other path that names no drive, or that
/// holds a character that no file name can hold.
[[nodiscard]] std::string absolute_system_path(std::string_view absolute);

/// True when path is inside directory, both canonical paths of the system (canonical_path()).
[[nodiscard]] bool is_inside(std::string_view path, std::string_view directory) noexcept;

} // namespace openxisf::detail
