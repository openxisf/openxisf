// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <openxisf/error.h>
#include <openxisf/io.h>

#include "core/quote.h"
#include "core/utf8.h"
#include "io/native_file.h"
#include "io/paths.h"

#include <memory>
#include <string>
#include <utility>

namespace openxisf {

namespace {

// A relative path leads to a file inside the directory once both are canonical, so that neither .. nor a symbolic
// link takes it out (spec §10.3 lets a path hold both).
std::unique_ptr<input_source> open_inside(const std::string& directory, const std::string& relative)
{
    const std::string base = detail::native_file::canonical_path(directory);
    const std::string target = detail::native_file::canonical_path(detail::relative_system_path(directory, relative));
    if (!detail::is_inside(target, base)) {
        throw unsupported_error(errc::location_not_allowed, "the path " + detail::quote(relative) + " leads to " +
                                                                target +
                                                                ", outside the directory of the header file, " + base);
    }
    return std::make_unique<file_source>(target);
}

} // namespace

external_resolver file_resolver(std::string_view header_directory, file_resolver_options options)
{
    if (header_directory.empty()) {
        throw usage_error(errc::invalid_argument, "the directory of the header file is empty");
    }
    if (!detail::is_valid_utf8(header_directory)) {
        throw usage_error(errc::invalid_utf8,
                          detail::quote(header_directory) + " is not a valid UTF-8 path of a directory");
    }
    return [directory = std::string(header_directory),
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
