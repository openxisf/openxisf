// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace openxisf::test {

/// The UTF-8 form of a path. std::filesystem::path::string() would give the ANSI code page on Windows.
[[nodiscard]] std::string utf8(const std::filesystem::path& path);

/// The path that UTF-8 text names, built without the ANSI code page on Windows.
[[nodiscard]] std::filesystem::path path_of(std::string_view text);

/// An absolute path in the form that the file functions of Windows accept beyond 260 characters, with the \\?\ prefix.
/// Unchanged on other systems.
[[nodiscard]] std::filesystem::path long_form(const std::filesystem::path& path);

/// A new, empty directory in the temporary directory of the system, removed with its content on destruction, so that
/// a test leaves nothing behind whether it passes or fails.
class temp_directory
{
public:
    temp_directory();
    ~temp_directory();

    temp_directory(const temp_directory&) = delete;
    temp_directory& operator=(const temp_directory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

    /// The UTF-8 path of the entry called name, which is UTF-8 too.
    [[nodiscard]] std::string file(std::string_view name) const;

private:
    std::filesystem::path path_;
};

} // namespace openxisf::test
