// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Every parser of the plain-text grammar (spec §8.3) and of the data encodings, on the same input, and the conversions
// between UTF-8 and UTF-16. A parser either accepts the text or throws invalid_data_error, and whatever it accepts must
// format back to text that parses to the same value. Any other exception escapes and fails the run.

#include "core/text_grammar.h"

#include <openxisf/error.h>

#include "core/data_encoding.h"
#include "core/utf16.h"
#include "core/utf8.h"
#include "core/uuid.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace detail = openxisf::detail;
using openxisf::errc;
using openxisf::invalid_data_error;

// Stops the run, so that the fuzzer reports the input that broke a property.
void require(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

template <detail::xisf_integer T> void check_integer(std::string_view text)
{
    std::optional<T> value;
    try {
        value = detail::parse_integer<T>(text);
    } catch (const invalid_data_error& failure) {
        require(failure.code() == errc::invalid_integer || failure.code() == errc::value_out_of_range);
        return;
    }
    require(detail::parse_integer<T>(detail::format_integer(*value)) == *value);
}

template <detail::xisf_float T> bool same_value(T a, T b)
{
    if (std::isnan(a)) {
        return std::isnan(b);
    }
    if constexpr (sizeof(T) == 4) {
        return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
    } else {
        return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
    }
}

template <detail::xisf_float T> void check_float(std::string_view text)
{
    const bool grammar = detail::is_float_text(text);
    T value = 0;
    try {
        value = detail::parse_float<T>(text);
    } catch (const invalid_data_error& failure) {
        // Text that follows the grammar fails only when the value is too large for T.
        require(failure.code() == (grammar ? errc::value_out_of_range : errc::invalid_float));
        return;
    }
    require(grammar);
    const std::string formatted = detail::format_float(value);
    require(detail::is_float_text(formatted));
    require(same_value(detail::parse_float<T>(formatted), value));
}

void check_boolean(std::string_view text)
{
    bool value = false;
    try {
        value = detail::parse_boolean(text);
    } catch (const invalid_data_error& failure) {
        require(failure.code() == errc::invalid_boolean);
        return;
    }
    require(detail::parse_boolean(detail::format_boolean(value)) == value);
}

std::string without_white_space(std::string_view text)
{
    std::string result;
    for (const char c : text) {
        if (!detail::is_white_space(c)) {
            result += c;
        }
    }
    return result;
}

// The decoders accept the canonical form only, so encoding the data gives back the text without its white space.
void check_base64(std::string_view text)
{
    std::vector<std::byte> data;
    try {
        data = detail::decode_base64(text);
    } catch (const invalid_data_error& failure) {
        require(failure.code() == errc::invalid_base64);
        return;
    }
    require(detail::encode_base64(data) == without_white_space(text));
}

void check_hex(std::string_view text)
{
    std::vector<std::byte> data;
    try {
        data = detail::decode_hex(text);
    } catch (const invalid_data_error& failure) {
        require(failure.code() == errc::invalid_hex);
        return;
    }
    require(detail::encode_hex(data) == without_white_space(text));
}

void check_uuid(std::string_view text)
{
    detail::uuid id{};
    try {
        id = detail::parse_uuid(text);
    } catch (const invalid_data_error& failure) {
        require(failure.code() == errc::invalid_uuid);
        return;
    }
    // The canonical form is the text in lowercase.
    const std::string formatted = detail::format_uuid(id);
    require(formatted.size() == text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        require(formatted[i] == (c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c));
    }
}

// Valid UTF-8 converts to UTF-16 and back unchanged, and nothing else converts.
void check_utf8_to_utf16(std::string_view text)
{
    const bool valid = detail::is_valid_utf8(text);
    std::u16string utf16;
    try {
        utf16 = detail::utf8_to_utf16(text);
    } catch (const invalid_data_error& failure) {
        require(failure.code() == errc::invalid_utf8 && !valid);
        return;
    }
    require(valid);
    require(detail::utf16_to_utf8(utf16) == text);
}

// The input read as UTF-16 code units in native byte order. What converts to UTF-8 is valid UTF-8 and converts back
// unchanged.
void check_utf16_to_utf8(std::string_view text)
{
    std::u16string utf16(text.size() / 2, u'\0');
    std::memcpy(utf16.data(), text.data(), utf16.size() * 2);
    std::string utf8;
    try {
        utf8 = detail::utf16_to_utf8(utf16);
    } catch (const invalid_data_error& failure) {
        require(failure.code() == errc::invalid_utf16);
        return;
    }
    require(detail::is_valid_utf8(utf8));
    require(detail::utf8_to_utf16(utf8) == utf16);
}

} // namespace

// The entry point that libFuzzer, and the replay driver, call for every input. Its name is fixed by libFuzzer.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::string_view text(reinterpret_cast<const char*>(data), size);

    check_integer<std::int8_t>(text);
    check_integer<std::uint8_t>(text);
    check_integer<std::int16_t>(text);
    check_integer<std::uint16_t>(text);
    check_integer<std::int32_t>(text);
    check_integer<std::uint32_t>(text);
    check_integer<std::int64_t>(text);
    check_integer<std::uint64_t>(text);
    check_integer<openxisf::int128>(text);
    check_integer<openxisf::uint128>(text);
    check_float<float>(text);
    check_float<double>(text);
    check_boolean(text);
    check_base64(text);
    check_hex(text);
    check_uuid(text);
    check_utf8_to_utf16(text);
    check_utf16_to_utf8(text);
    return 0;
}
