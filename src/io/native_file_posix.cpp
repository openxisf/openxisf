// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include <openxisf/error.h>

#include "core/checked_math.h"
#include "core/utf8.h"
#include "io/native_file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <string_view>
#include <system_error>
#include <utility>

namespace openxisf::detail {

namespace {

// pread() and pwrite() take at most SSIZE_MAX bytes, and Linux transfers at most 2 GiB - 4 KiB in one call.
constexpr std::size_t max_transfer = std::size_t{1} << 30;

std::error_code last_error() noexcept
{
    return {errno, std::system_category()};
}

[[noreturn]] void fail(errc code, const std::string& message, std::error_code system, error_context context = {})
{
    // The text of strerror() follows the locale of the application, which may not be UTF-8.
    std::string reason = system.message();
    if (!is_valid_utf8(reason)) {
        reason = "error " + std::to_string(system.value());
    }
    throw io_error(code, message + ": " + reason, system, std::move(context));
}

[[noreturn]] void fail_not_regular(const std::string& path)
{
    throw io_error(errc::not_a_regular_file, path + " is not a regular file");
}

// The directory that holds the file at path.
std::string directory_of(const std::string& path)
{
    const std::size_t separator = path.find_last_of('/');
    if (separator == std::string::npos) {
        return ".";
    }
    return separator == 0 ? "/" : path.substr(0, separator);
}

// Makes a rename durable. Some file systems cannot flush a directory and say so with EINVAL or ENOTSUP; their renames
// are as durable as they get.
void flush_directory(const std::string& path)
{
    const std::string directory = directory_of(path);
    const int handle = ::open(directory.c_str(), O_RDONLY | O_CLOEXEC);
    if (handle < 0) {
        fail(errc::write_failed, "cannot open the directory of " + path, last_error());
    }
    const int result = ::fsync(handle);
    const std::error_code error = last_error();
    (void)::close(handle);
    if (result != 0 && error != std::errc::invalid_argument && error != std::errc::not_supported) {
        fail(errc::write_failed, "cannot flush the directory of " + path + " to disk", error);
    }
}

} // namespace

native_file native_file::open_for_reading(const std::string& path)
{
    // O_NONBLOCK keeps the open of a FIFO from waiting for a writer. It has no effect on regular files, which are the
    // only ones accepted.
    const int handle = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (handle < 0) {
        fail(errc::open_failed, "cannot open " + path, last_error());
    }
    native_file file(handle, path);
    struct stat status{};
    if (::fstat(handle, &status) != 0) {
        fail(errc::open_failed, "cannot open " + path, last_error());
    }
    if (!S_ISREG(status.st_mode)) {
        fail_not_regular(path);
    }
    return file;
}

std::optional<native_file> native_file::create_new(const std::string& path, std::string name)
{
    // Read and write permissions for everyone the umask allows, as for any new file.
    const int handle = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (handle < 0) {
        const std::error_code error = last_error();
        if (error == std::errc::file_exists) {
            return std::nullopt;
        }
        fail(errc::open_failed, "cannot create a temporary file next to " + name, error);
    }
    return native_file(handle, std::move(name));
}

std::uint64_t native_file::size() const
{
    struct stat status{};
    if (::fstat(handle_, &status) != 0) {
        fail(errc::read_failed, "cannot read the size of " + name_, last_error());
    }
    return checked_cast<std::uint64_t>(status.st_size);
}

void native_file::read_at(std::uint64_t offset, std::span<std::byte> destination) const
{
    std::uint64_t position = offset;
    while (!destination.empty()) {
        const std::size_t chunk = std::min(destination.size(), max_transfer);
        const ssize_t count = ::pread(handle_, destination.data(), chunk, checked_cast<off_t>(position));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            fail(errc::read_failed, "cannot read " + name_, last_error(), {.offset = position});
        }
        if (count == 0) {
            throw io_error(errc::end_of_data, name_ + " ends before the data that was asked for", {},
                           {.offset = position});
        }
        const auto transferred = static_cast<std::size_t>(count);
        destination = destination.subspan(transferred);
        position += transferred;
    }
}

void native_file::write_at(std::uint64_t offset, std::span<const std::byte> data) const
{
    std::uint64_t position = offset;
    while (!data.empty()) {
        const std::size_t chunk = std::min(data.size(), max_transfer);
        const ssize_t count = ::pwrite(handle_, data.data(), chunk, checked_cast<off_t>(position));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            fail(errc::write_failed, "cannot write " + name_, last_error(), {.offset = position});
        }
        const auto transferred = static_cast<std::size_t>(count);
        data = data.subspan(transferred);
        position += transferred;
    }
}

void native_file::flush_to_disk() const
{
#if defined(__APPLE__)
    // On macOS fsync() leaves the data in the cache of the drive. F_FULLFSYNC flushes it too, where the file system
    // supports it.
    if (::fcntl(handle_, F_FULLFSYNC) == 0) {
        return;
    }
#endif
    if (::fsync(handle_) != 0) {
        fail(errc::write_failed, "cannot flush " + name_ + " to disk", last_error());
    }
}

void native_file::close()
{
    // POSIX leaves the descriptor unspecified after a failed close(), and Linux closes it in every case, so it is
    // never closed twice.
    const int handle = std::exchange(handle_, closed_handle);
    if (handle != closed_handle && ::close(handle) != 0) {
        fail(errc::write_failed, "cannot write " + name_, last_error());
    }
}

void native_file::reset() noexcept
{
    const int handle = std::exchange(handle_, closed_handle);
    if (handle != closed_handle) {
        (void)::close(handle);
    }
}

void native_file::commit(const std::string& path, const std::string& target, bool flush_to_disk)
{
    if (flush_to_disk) {
        this->flush_to_disk();
    }
    close();
    if (std::rename(path.c_str(), target.c_str()) != 0) {
        fail(errc::write_failed, "cannot replace " + target, last_error());
    }
    if (flush_to_disk) {
        flush_directory(target);
    }
}

void native_file::discard(const std::string& path) noexcept
{
    // After a successful rename nothing is left at path, and unlink() fails harmlessly.
    reset();
    (void)::unlink(path.c_str());
}

} // namespace openxisf::detail
