// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The command-line arguments of the sample programs in UTF-8, the text encoding of the OpenXISF API on every platform.
// On Windows, main() receives them in the ANSI code page, which cannot represent every character of a file name, so
// they are taken from CommandLineToArgvW() in UTF-16 and converted.

#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
// shellapi.h needs the declarations of windows.h before it, so the two keep this order.
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif

namespace example {

#if defined(_WIN32)

/// text, in UTF-8, as UTF-16 for the functions of Windows that take paths, such as _wfopen().
inline std::wstring to_wide(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    const int size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        throw std::runtime_error("the text is not UTF-8");
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

inline std::string to_utf8(const wchar_t* wide)
{
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        throw std::runtime_error("an argument is not valid UTF-16");
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, text.data(), size, nullptr, nullptr);
    text.pop_back(); // The terminating null character.
    return text;
}

#endif

/// The arguments of the program, its name first, in UTF-8.
inline std::vector<std::string> utf8_arguments([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
#if defined(_WIN32)
    int count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide == nullptr) {
        throw std::runtime_error("cannot read the command line");
    }
    std::vector<std::string> arguments;
    try {
        for (int i = 0; i < count; ++i) {
            arguments.push_back(to_utf8(wide[i]));
        }
    } catch (...) {
        LocalFree(static_cast<HLOCAL>(wide));
        throw;
    }
    LocalFree(static_cast<HLOCAL>(wide));
    return arguments;
#else
    return {argv, argv + argc};
#endif
}

} // namespace example
