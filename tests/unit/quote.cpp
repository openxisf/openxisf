// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Untrusted text in error messages.

#include "core/quote.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using openxisf::detail::quote;

TEST(quote, keeps_printable_ascii_between_single_quotes)
{
    EXPECT_EQ(quote(""), "''");
    EXPECT_EQ(quote("Abc 123 ~!"), "'Abc 123 ~!'");
}

TEST(quote, escapes_its_quote_and_the_backslash)
{
    EXPECT_EQ(quote(R"(it's a\b)"), R"('it\'s a\\b')");
}

TEST(quote, writes_other_bytes_in_hexadecimal)
{
    // Control characters, DEL and the bytes of UTF-8 above ASCII (é).
    EXPECT_EQ(quote(std::string("\x00\t\x1f\x7f\xc3\xa9", 6)), R"('\x00\x09\x1f\x7f\xc3\xa9')");
}

TEST(quote, cuts_the_text_after_40_bytes)
{
    const std::string forty(40, 'a');
    EXPECT_EQ(quote(forty), "'" + forty + "'");
    EXPECT_EQ(quote(forty + "b"), "'" + forty + "'...");
    // The cut counts bytes of the text, not characters of the quote.
    const std::string escaped(41, '\x01');
    std::string expected = "'";
    for (int i = 0; i < 40; ++i) {
        expected += R"(\x01)";
    }
    EXPECT_EQ(quote(escaped), expected + "'...");
}

} // namespace
