// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#pragma once

#include <openxisf/error.h>

#include <string>
#include <string_view>
#include <vector>

namespace openxisf::detail {

/// Throws the exception for an error found in a unit: integrity_error for data that fail a check, unsupported_error for
/// valid data that OpenXISF cannot handle, limit_error for data beyond a limit, and invalid_data_error for anything
/// else.
[[noreturn]] void throw_unit_error(errc code, std::string_view message, error_context context);

/// The diagnostics recorded while a unit is opened (spec §7). In strict mode, an error fails the open instead.
class diagnostic_log
{
public:
    explicit diagnostic_log(bool strict) noexcept : strict_(strict) {}

    /// Something the specification allows was ignored.
    void info(errc code, std::string message, error_context context = {});

    /// A harmless violation of the specification was tolerated.
    void warning(errc code, std::string message, error_context context = {});

    /// An object of the unit is unavailable. Throws the exception of throw_unit_error() instead in strict mode.
    void error(errc code, std::string message, error_context context = {});

    [[nodiscard]] const std::vector<diagnostic>& entries() const noexcept
    {
        return entries_;
    }

    /// Moves the diagnostics out of the log, which is empty afterwards.
    [[nodiscard]] std::vector<diagnostic> release() noexcept;

private:
    void add(openxisf::severity level, errc code, std::string message, error_context context);

    bool strict_;
    std::vector<diagnostic> entries_;
};

} // namespace openxisf::detail
