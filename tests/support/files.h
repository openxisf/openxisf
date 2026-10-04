// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#if defined(_WIN32)
#include <share.h>
#endif

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>

// Files for the tests, written and read with the standard library, independently of the library under test.

namespace openxisf::test {

inline void write_file(const std::filesystem::path& path, std::span<const std::byte> data)
{
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    file.close();
    if (file.fail()) {
        throw std::runtime_error("cannot write the test file");
    }
}

[[nodiscard]] inline std::string read_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot read the test file");
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// A C stream on a file, closed on destruction. The path reaches the system in UTF-16 on Windows.
class c_file
{
public:
    c_file(const std::filesystem::path& path, const char* mode) : stream_(open(path, mode))
    {
        if (stream_ == nullptr) {
            throw std::runtime_error("cannot open the test file");
        }
    }

    ~c_file()
    {
        (void)std::fclose(stream_); // NOLINT(cppcoreguidelines-owning-memory): the stream is owned
    }

    c_file(const c_file&) = delete;
    c_file& operator=(const c_file&) = delete;

    [[nodiscard]] std::FILE* get() const noexcept
    {
        return stream_;
    }

private:
    static std::FILE* open(const std::filesystem::path& path, const char* mode)
    {
#if defined(_WIN32)
        // Shared for reading and writing, as fopen() shares on POSIX systems, so that tests can resize the file.
        const std::wstring wide_mode(mode, mode + std::strlen(mode));
        return _wfsopen(path.c_str(), wide_mode.c_str(), _SH_DENYNO);
#else
        return std::fopen(path.c_str(), mode); // NOLINT(cppcoreguidelines-owning-memory): c_file closes it
#endif
    }

    std::FILE* stream_;
};

} // namespace openxisf::test
