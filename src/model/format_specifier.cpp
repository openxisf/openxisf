// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "model/format_specifier.h"

#include <openxisf/error.h>

#include "core/quote.h"
#include "core/text_grammar.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace openxisf::detail {

namespace {

template <typename Enum> struct keyword
{
    std::string_view name{};
    Enum value{};
};

constexpr std::array<keyword<format_align>, 3> align_keywords{{
    {.name = "right", .value = format_align::right},
    {.name = "left", .value = format_align::left},
    {.name = "center", .value = format_align::center},
}};

constexpr std::array<keyword<format_sign>, 2> sign_keywords{{
    {.name = "auto", .value = format_sign::automatic},
    {.name = "force", .value = format_sign::force},
}};

constexpr std::array<keyword<format_notation>, 3> float_keywords{{
    {.name = "auto", .value = format_notation::automatic},
    {.name = "scientific", .value = format_notation::scientific},
    {.name = "fixed", .value = format_notation::fixed},
}};

constexpr std::array<keyword<format_bool>, 2> bool_keywords{{
    {.name = "alpha", .value = format_bool::alpha},
    {.name = "numeric", .value = format_bool::numeric},
}};

constexpr std::array<keyword<format_base>, 4> base_keywords{{
    {.name = "dec", .value = format_base::decimal},
    {.name = "bin", .value = format_base::binary},
    {.name = "oct", .value = format_base::octal},
    {.name = "hex", .value = format_base::hexadecimal},
}};

template <typename Enum, std::size_t N>
std::optional<Enum> keyword_value(const std::array<keyword<Enum>, N>& keywords, std::string_view name) noexcept
{
    for (const keyword<Enum>& entry : keywords) {
        if (entry.name == name) {
            return entry.value;
        }
    }
    return std::nullopt;
}

template <typename Enum, std::size_t N>
std::string_view keyword_name(const std::array<keyword<Enum>, N>& keywords, Enum value) noexcept
{
    for (const keyword<Enum>& entry : keywords) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return {};
}

// The tokens of spec §8.4.3, in its order.
enum class token : std::uint8_t
{
    width,
    fill,
    align,
    sign,
    precision,
    floating,
    boolean,
    base,
    unit,
};

constexpr std::array<keyword<token>, 9> token_keywords{{
    {.name = "width", .value = token::width},
    {.name = "fill", .value = token::fill},
    {.name = "align", .value = token::align},
    {.name = "sign", .value = token::sign},
    {.name = "precision", .value = token::precision},
    {.name = "float", .value = token::floating},
    {.name = "bool", .value = token::boolean},
    {.name = "base", .value = token::base},
    {.name = "unit", .value = token::unit},
}};

class specifier_parser
{
public:
    explicit specifier_parser(std::string_view text) noexcept : text_(text) {}

    property_format parse()
    {
        // White space is irrelevant anywhere in a format specifier, the unit included.
        std::string compact;
        for (const char c : text_) {
            if (!is_white_space(c)) {
                compact += c;
            }
        }
        std::string_view rest = compact;
        while (true) {
            const std::size_t end = rest.find(';');
            apply(rest.substr(0, end));
            if (end == std::string_view::npos) {
                return format_;
            }
            rest.remove_prefix(end + 1);
        }
    }

private:
    [[noreturn]] void fail(const std::string& reason) const
    {
        throw invalid_data_error(errc::invalid_format_specifier,
                                 quote(text_) + " is not a property format specifier: " + reason);
    }

    void apply(std::string_view text)
    {
        const std::size_t colon = text.find(':');
        if (colon == std::string_view::npos) {
            fail(text.empty() ? "a token is empty" : "the token " + quote(text) + " has no value");
        }
        const std::string_view name = text.substr(0, colon);
        const std::string_view value = text.substr(colon + 1);
        const std::optional<token> which = keyword_value(token_keywords, name);
        if (!which) {
            fail(quote(name) + " is not a format token");
        }
        const auto bit = 1U << static_cast<unsigned>(*which);
        if ((seen_ & bit) != 0) {
            fail("the token " + std::string(name) + " is given twice");
        }
        seen_ |= bit;

        switch (*which) {
        case token::width:
            format_.width = count(name, value);
            break;
        case token::fill:
            // A printable ASCII character; white space and the semicolon cannot get here.
            if (value.size() != 1 || value[0] < '!' || value[0] > '~') {
                fail("the fill is not one printable ASCII character");
            }
            format_.fill = value[0];
            break;
        case token::align:
            format_.align = choice(align_keywords, name, value);
            break;
        case token::sign:
            format_.sign = choice(sign_keywords, name, value);
            break;
        case token::precision:
            format_.precision = count(name, value);
            break;
        case token::floating:
            format_.notation = choice(float_keywords, name, value);
            break;
        case token::boolean:
            format_.boolean = choice(bool_keywords, name, value);
            break;
        case token::base:
            format_.base = choice(base_keywords, name, value);
            break;
        case token::unit:
            if (value.empty()) {
                fail("the unit is empty");
            }
            format_.unit = value;
            break;
        }
    }

    std::uint32_t count(std::string_view name, std::string_view value) const
    {
        try {
            return parse_integer<std::uint32_t>(value);
        } catch (const invalid_data_error&) {
            fail("the " + std::string(name) + " " + quote(value) + " is not an unsigned 32-bit integer");
        }
    }

    template <typename Enum, std::size_t N>
    Enum choice(const std::array<keyword<Enum>, N>& keywords, std::string_view name, std::string_view value) const
    {
        const std::optional<Enum> found = keyword_value(keywords, value);
        if (!found) {
            fail(quote(value) + " is not a value of the token " + std::string(name));
        }
        return *found;
    }

    std::string_view text_;
    property_format format_{};
    unsigned seen_ = 0;
};

void append_token(std::string& text, std::string_view name, std::string_view value)
{
    if (!text.empty()) {
        text += ';';
    }
    text += name;
    text += ':';
    text += value;
}

} // namespace

property_format parse_format_specifier(std::string_view text)
{
    return specifier_parser(text).parse();
}

std::string format_specifier_text(const property_format& format)
{
    const property_format defaults{};
    std::string text;
    if (format.width != defaults.width) {
        append_token(text, "width", format_integer(format.width));
    }
    if (format.fill != defaults.fill) {
        append_token(text, "fill", std::string_view(&format.fill, 1));
    }
    if (format.align != defaults.align) {
        append_token(text, "align", keyword_name(align_keywords, format.align));
    }
    if (format.sign != defaults.sign) {
        append_token(text, "sign", keyword_name(sign_keywords, format.sign));
    }
    if (format.precision != defaults.precision) {
        append_token(text, "precision", format_integer(format.precision));
    }
    if (format.notation != defaults.notation) {
        append_token(text, "float", keyword_name(float_keywords, format.notation));
    }
    if (format.boolean != defaults.boolean) {
        append_token(text, "bool", keyword_name(bool_keywords, format.boolean));
    }
    if (format.base != defaults.base) {
        append_token(text, "base", keyword_name(base_keywords, format.base));
    }
    if (!format.unit.empty()) {
        append_token(text, "unit", format.unit);
    }
    return text;
}

} // namespace openxisf::detail
