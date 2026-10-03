// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "support/sparse_file.h"

#if defined(_WIN32)
#include <windows.h>
#include <winioctl.h>
#endif

#include <fstream>
#include <ios>
#include <stdexcept>
#include <system_error>

namespace openxisf::test {

bool create_sparse_file(const std::filesystem::path& path, std::uint64_t size)
{
    if (!std::ofstream(path, std::ios::binary)) {
        return false;
    }
#if defined(_WIN32)
    // NTFS allocates the clusters of a normal file when it grows, and zeros them when bytes are written beyond them.
    // The size is set through the handle: the C runtime under std::filesystem::resize_file() of MinGW writes the zeros.
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD returned = 0;
    LARGE_INTEGER end{};
    end.QuadPart = static_cast<LONGLONG>(size);
    const bool created =
        DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) != FALSE &&
        SetFilePointerEx(handle, end, nullptr, FILE_BEGIN) != FALSE && SetEndOfFile(handle) != FALSE;
    (void)CloseHandle(handle);
    return created;
#else
    std::error_code error;
    std::filesystem::resize_file(path, size, error);
    return !error;
#endif
}

void write_at(const std::filesystem::path& path, std::uint64_t offset, std::span<const std::byte> data)
{
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    file.seekp(static_cast<std::streamoff>(offset));
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    file.close();
    if (file.fail()) {
        throw std::runtime_error("cannot write the test file");
    }
}

} // namespace openxisf::test
