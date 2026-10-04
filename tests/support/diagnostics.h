// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/error.h>

#include <gtest/gtest.h>

#include <span>
#include <string>
#include <string_view>

namespace openxisf::test {

[[nodiscard]] inline std::string_view severity_name(severity level)
{
    switch (level) {
    case severity::info:
        return "info";
    case severity::warning:
        return "warning";
    case severity::error:
        return "error";
    }
    return "?";
}

/// The diagnostics as text, one per line, for the messages of failed assertions.
[[nodiscard]] inline std::string describe(std::span<const diagnostic> diagnostics)
{
    std::string text;
    for (const diagnostic& entry : diagnostics) {
        text += "  ";
        text += severity_name(entry.severity);
        text += " " + std::to_string(static_cast<int>(entry.code)) + ": " + entry.message;
        text += " [element '" + entry.context.element + "', attribute '" + entry.context.attribute + "'";
        if (entry.context.offset) {
            text += ", offset " + std::to_string(*entry.context.offset);
        }
        text += "]\n";
    }
    return text;
}

/// Success when there is no diagnostic.
[[nodiscard]] inline testing::AssertionResult no_diagnostics(std::span<const diagnostic> diagnostics)
{
    if (diagnostics.empty()) {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure() << "unexpected diagnostics:\n" << describe(diagnostics);
}

/// Success when there is exactly one diagnostic, with the given severity and code, about the element at the given path,
/// which is empty for a diagnostic about no element.
[[nodiscard]] inline testing::AssertionResult single_diagnostic(std::span<const diagnostic> diagnostics, severity level,
                                                                errc code, std::string_view element = {})
{
    if (diagnostics.size() == 1 && diagnostics.front().severity == level && diagnostics.front().code == code &&
        diagnostics.front().context.element == element) {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure() << "expected one diagnostic of code " << static_cast<int>(code) << " about '"
                                       << element << "', got:\n"
                                       << describe(diagnostics);
}

} // namespace openxisf::test
