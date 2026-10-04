// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "core/diagnostic_log.h"

#include <utility>

namespace openxisf::detail {

void throw_unit_error(errc code, std::string_view message, error_context context)
{
    switch (code) {
    case errc::checksum_mismatch:
    case errc::corrupt_compressed_data:
        throw integrity_error(code, message, std::move(context));
    case errc::unsupported_version:
    case errc::unsupported_location:
    case errc::unsupported_checksum:
    case errc::unsupported_compression:
    case errc::unsupported_property_type:
    case errc::unsupported_sample_format:
    case errc::unsupported_color_space:
    case errc::codec_failure:
    case errc::location_not_allowed:
        throw unsupported_error(code, message, std::move(context));
    case errc::allocation_too_large:
    case errc::zstd_window_too_large:
    case errc::ancillary_data_too_large:
    case errc::too_many_index_nodes:
    case errc::too_many_external_files:
        throw limit_error(code, message, std::move(context));
    case errc::open_failed:
    case errc::not_a_regular_file:
    case errc::read_failed:
    case errc::end_of_data:
        throw io_error(code, message, {}, std::move(context));
    default:
        throw invalid_data_error(code, message, std::move(context));
    }
}

void diagnostic_log::info(errc code, std::string message, error_context context)
{
    add(severity::info, code, std::move(message), std::move(context));
}

void diagnostic_log::warning(errc code, std::string message, error_context context)
{
    add(severity::warning, code, std::move(message), std::move(context));
}

void diagnostic_log::error(errc code, std::string message, error_context context)
{
    if (strict_) {
        throw_unit_error(code, message, std::move(context));
    }
    add(severity::error, code, std::move(message), std::move(context));
}

std::vector<diagnostic> diagnostic_log::release() noexcept
{
    return std::exchange(entries_, {});
}

void diagnostic_log::add(openxisf::severity level, errc code, std::string message, error_context context)
{
    entries_.push_back({.severity = level, .code = code, .message = std::move(message), .context = std::move(context)});
}

} // namespace openxisf::detail
