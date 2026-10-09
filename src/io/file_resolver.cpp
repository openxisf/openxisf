// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <openxisf/error.h>
#include <openxisf/io.h>

#include "core/quote.h"
#include "io/native_file.h"
#include "io/paths.h"
#include "io/range_check.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace openxisf {

namespace {

// A file inside the directory of the header file, read as file_source reads one.
class confined_file final : public input_source
{
public:
    explicit confined_file(detail::native_file file) : file_(std::move(file)), size_(file_.size()) {}

    [[nodiscard]] std::uint64_t size() const override
    {
        return size_;
    }

    void read(std::uint64_t offset, std::span<std::byte> destination) const override
    {
        detail::check_read_range(size_, offset, destination.size(), file_.name());
        file_.read_at(offset, destination);
    }

    [[nodiscard]] bool supports_concurrent_reads() const override
    {
        return true;
    }

    [[nodiscard]] std::string description() const override
    {
        return file_.name();
    }

private:
    detail::native_file file_;
    std::uint64_t size_;
};

// A relative path leads to a file inside the directory once both are canonical, so that neither .. nor a symbolic
// link takes it out (spec §10.3 lets a path hold both).
std::unique_ptr<input_source> open_inside(const std::string& directory, const std::string& relative)
{
    const std::string base = detail::native_file::find(directory).name();
    const detail::native_file found = detail::native_file::find(detail::relative_system_path(directory, relative));
    if (!detail::is_inside(found.name(), base)) {
        throw unsupported_error(errc::location_not_allowed, "the path " + detail::quote(relative) + " leads to " +
                                                                found.name() +
                                                                ", outside the directory of the header file, " + base);
    }
    // The file whose path was checked, named by that path, which on Windows starts with \\?\. Windows reads it through
    // the handle that found it; other systems open that path again.
    return std::make_unique<confined_file>(found.reopen_for_reading());
}

} // namespace

external_resolver file_resolver(std::string_view header_directory, file_resolver_options options)
{
    detail::check_path_argument(header_directory, "the directory of the header file");
    // The directory that the path names now, whatever the current directory is when a unit is read again.
    return [directory = detail::absolute_path(std::string(header_directory)),
            options](const external_reference& reference) -> std::unique_ptr<input_source> {
        switch (reference.form) {
        case location_form::url:
            throw unsupported_error(errc::unsupported_location, "the URL " + detail::quote(reference.location) +
                                                                    " is not opened: URLs are opened only by a "
                                                                    "resolver of the application");
        case location_form::absolute_path:
            if (!options.absolute_paths) {
                throw unsupported_error(errc::location_not_allowed, "the absolute path " +
                                                                        detail::quote(reference.location) +
                                                                        " is not opened, since absolute paths are not "
                                                                        "allowed");
            }
            return std::make_unique<file_source>(detail::absolute_system_path(reference.location));
        case location_form::relative_path:
            break;
        }
        return open_inside(directory, reference.location);
    };
}

} // namespace openxisf
