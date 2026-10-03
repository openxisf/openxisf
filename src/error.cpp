// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include <openxisf/error.h>

#include <utility>

namespace openxisf {

namespace {

std::string describe(std::string_view message, const error_context& context)
{
    std::string where;
    const auto add = [&where](std::string_view part) {
        where += where.empty() ? " (" : ", ";
        where += part;
    };
    if (!context.element.empty()) {
        add("element " + context.element);
    }
    if (!context.attribute.empty()) {
        add("attribute " + context.attribute);
    }
    if (context.offset) {
        add("offset " + std::to_string(*context.offset));
    }
    if (!where.empty()) {
        where += ')';
    }
    return std::string(message) + where;
}

} // namespace

// The destructors are the key functions: defining them here emits the type information once, in the library, so
// that exceptions are caught across shared-library boundaries.

error::error(errc code, std::string_view message, error_context context)
    : std::runtime_error(describe(message, context)), code_(code),
      context_(std::make_shared<const error_context>(std::move(context)))
{}

error::~error() = default;

errc error::code() const noexcept
{
    return code_;
}

const error_context& error::context() const noexcept
{
    return *context_;
}

io_error::io_error(errc code, std::string_view message, std::error_code system_code, error_context context)
    : error(code, message, std::move(context)), system_code_(system_code)
{}

io_error::~io_error() = default;

std::error_code io_error::system_code() const noexcept
{
    return system_code_;
}

invalid_data_error::~invalid_data_error() = default;
integrity_error::~integrity_error() = default;
unsupported_error::~unsupported_error() = default;
limit_error::~limit_error() = default;
validation_error::~validation_error() = default;
usage_error::~usage_error() = default;
cancelled_error::~cancelled_error() = default;

} // namespace openxisf
