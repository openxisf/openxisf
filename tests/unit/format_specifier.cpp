// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

// The grammar of property format specifiers (spec §8.4.3).

#include "model/format_specifier.h"

#include "support/throws.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

using openxisf::errc;
using openxisf::format_align;
using openxisf::format_base;
using openxisf::format_bool;
using openxisf::format_notation;
using openxisf::format_sign;
using openxisf::invalid_data_error;
using openxisf::property_format;
using openxisf::detail::format_specifier_text;
using openxisf::detail::parse_format_specifier;
using openxisf::test::throws;

TEST(format_specifier, the_examples_of_the_specification)
{
    // Spec §11.1.2.
    EXPECT_EQ(parse_format_specifier("width:8;align:center"),
              (property_format{.width = 8, .align = format_align::center}));
    EXPECT_EQ(
        parse_format_specifier("width:6;precision:2;float:fixed;sign:force"),
        (property_format{.width = 6, .sign = format_sign::force, .precision = 2, .notation = format_notation::fixed}));
    EXPECT_EQ(parse_format_specifier("width:8;base:hex;fill:."),
              (property_format{.width = 8, .fill = '.', .base = format_base::hexadecimal}));
    EXPECT_EQ(parse_format_specifier("width:10;precision:3"), (property_format{.width = 10, .precision = 3}));
    // Spec §8.4.3.
    EXPECT_EQ(parse_format_specifier("unit:m/s").unit, "m/s");
    EXPECT_EQ(parse_format_specifier("unit:erg*s^-1*cm^2*Hz^-1").unit, "erg*s^-1*cm^2*Hz^-1");
}

TEST(format_specifier, every_value_of_every_token)
{
    EXPECT_EQ(parse_format_specifier("align:left").align, format_align::left);
    EXPECT_EQ(parse_format_specifier("align:right").align, format_align::right);
    EXPECT_EQ(parse_format_specifier("align:center").align, format_align::center);
    EXPECT_EQ(parse_format_specifier("sign:auto").sign, format_sign::automatic);
    EXPECT_EQ(parse_format_specifier("sign:force").sign, format_sign::force);
    EXPECT_EQ(parse_format_specifier("float:auto").notation, format_notation::automatic);
    EXPECT_EQ(parse_format_specifier("float:scientific").notation, format_notation::scientific);
    EXPECT_EQ(parse_format_specifier("float:fixed").notation, format_notation::fixed);
    EXPECT_EQ(parse_format_specifier("bool:alpha").boolean, format_bool::alpha);
    EXPECT_EQ(parse_format_specifier("bool:numeric").boolean, format_bool::numeric);
    EXPECT_EQ(parse_format_specifier("base:bin").base, format_base::binary);
    EXPECT_EQ(parse_format_specifier("base:oct").base, format_base::octal);
    EXPECT_EQ(parse_format_specifier("base:dec").base, format_base::decimal);
    EXPECT_EQ(parse_format_specifier("base:hex").base, format_base::hexadecimal);
    EXPECT_EQ(parse_format_specifier("fill::").fill, ':');
    EXPECT_EQ(parse_format_specifier("fill:~").fill, '~');
    EXPECT_EQ(parse_format_specifier("width:0").width, 0U);
    EXPECT_EQ(parse_format_specifier("precision:0x10").precision, 16U);
    EXPECT_EQ(parse_format_specifier("width:4294967295").width, 4294967295U);
    EXPECT_EQ(parse_format_specifier("unit:km:s").unit, "km:s");
    EXPECT_EQ(parse_format_specifier("unit:µm").unit, "µm");
}

TEST(format_specifier, white_space_is_ignored_everywhere)
{
    EXPECT_EQ(parse_format_specifier(" width : 8 ;\talign:\ncenter "),
              (property_format{.width = 8, .align = format_align::center}));
    // So a unit cannot hold spaces: they go.
    EXPECT_EQ(parse_format_specifier("unit:m / s").unit, "m/s");
}

TEST(format_specifier, malformed_specifiers)
{
    for (const std::string_view text : {"",
                                        " ",
                                        ";",
                                        "width:8;",
                                        ";width:8",
                                        "width:8;;align:left",
                                        "width",
                                        "width:",
                                        "width:-1",
                                        "width:1.5",
                                        "width:4294967296",
                                        "Width:8",
                                        "colour:red",
                                        "fill:",
                                        "fill:ab",
                                        "fill: ",
                                        "fill:\x7f",
                                        "fill:\xC3\xA9",
                                        "align:middle",
                                        "align:Left",
                                        "sign:always",
                                        "float:general",
                                        "bool:yes",
                                        "base:16",
                                        "base:hexadecimal",
                                        "unit:",
                                        "width:8;width:9",
                                        "unit:m;unit:s",
                                        "precision:2;precision:2"}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_format_specifier, [text] {
            (void)parse_format_specifier(text);
        })) << text;
    }
}

TEST(format_specifier, the_text_of_a_format_has_the_tokens_that_differ_from_the_defaults)
{
    EXPECT_EQ(format_specifier_text({}), "");
    EXPECT_EQ(format_specifier_text(
                  {.width = 6, .sign = format_sign::force, .precision = 2, .notation = format_notation::fixed}),
              "width:6;sign:force;precision:2;float:fixed");
    const property_format every{.width = 8,
                                .fill = '.',
                                .align = format_align::center,
                                .sign = format_sign::force,
                                .precision = 3,
                                .notation = format_notation::scientific,
                                .boolean = format_bool::numeric,
                                .base = format_base::binary,
                                .unit = "m/s"};
    const std::string text = format_specifier_text(every);
    EXPECT_EQ(text,
              "width:8;fill:.;align:center;sign:force;precision:3;float:scientific;bool:numeric;base:bin;unit:m/s");
    EXPECT_EQ(parse_format_specifier(text), every);
    // A token that repeats a default is not written.
    EXPECT_EQ(format_specifier_text(parse_format_specifier("align:right;precision:6;width:5")), "width:5");
}

} // namespace
