// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

// Spec §11.7: ICCProfile elements, on constructed units.

#include <openxisf/error.h>
#include <openxisf/image.h>
#include <openxisf/reader.h>

#include "codec/compressed_block.h"
#include "codec/compression.h"
#include "core/data_encoding.h"
#include "support/bytes.h"
#include "support/diagnostics.h"
#include "support/fixture_builder.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

using openxisf::errc;
using openxisf::reader;
using openxisf::severity;
using openxisf::test::bytes;
using openxisf::test::header_xml;
using openxisf::test::image_xml;
using openxisf::test::no_diagnostics;
using openxisf::test::single_diagnostic;

// The bytes of a profile, whose content the reader keeps without looking at it: an ICC header starts with the size of
// the profile and has its signature at byte 36.
std::vector<std::byte> profile_bytes()
{
    std::vector<std::byte> profile = openxisf::test::pattern(132);
    profile[0] = std::byte{0};
    profile[1] = std::byte{0};
    profile[2] = std::byte{0};
    profile[3] = std::byte{132};
    const std::vector<std::byte> signature = bytes("acsp");
    std::ranges::copy(signature, profile.begin() + 36);
    return profile;
}

reader open_body(std::string_view body, openxisf::read_options options = {})
{
    return openxisf::test::open_header(header_xml(body), options);
}

reader open_attached(std::string_view body, const std::vector<std::vector<std::byte>>& blocks,
                     openxisf::read_options options = {})
{
    return openxisf::test::open_unit(openxisf::test::file_with_attachments(header_xml(body), blocks), options);
}

std::string inline_profile()
{
    return R"(<ICCProfile location="inline:base64">)" + openxisf::detail::encode_base64(profile_bytes()) +
           "</ICCProfile>";
}

TEST(conformance_icc, the_profile_bytes_are_kept_unaltered_in_every_block_form)
{
    const std::vector<std::byte> profile = profile_bytes();
    const reader attached = open_attached(image_xml({}, R"(<ICCProfile location="attachment:{0}"/>)"), {profile});
    EXPECT_TRUE(no_diagnostics(attached.diagnostics()));
    EXPECT_EQ(attached.image(0).icc_profile, profile);

    const reader inline_block = open_body(image_xml({}, inline_profile()));
    EXPECT_TRUE(no_diagnostics(inline_block.diagnostics()));
    EXPECT_EQ(inline_block.image(0).icc_profile, profile);

    const reader embedded =
        open_body(image_xml({}, R"(<ICCProfile location="embedded"><Data encoding="hex">)" +
                                    openxisf::detail::encode_hex(profile) + "</Data></ICCProfile>"));
    EXPECT_TRUE(no_diagnostics(embedded.diagnostics()));
    EXPECT_EQ(embedded.image(0).icc_profile, profile);
}

TEST(conformance_icc, a_compressed_profile_is_decompressed)
{
    // The second example of spec §11.7 is a zlib-compressed inline profile. Zeros make the profile compress.
    std::vector<std::byte> profile = profile_bytes();
    profile.resize(1024);
    const openxisf::detail::compressed_block block =
        openxisf::detail::compress_block(profile, {.codec = openxisf::detail::compression_codec::zlib});
    ASSERT_LT(block.data.size(), profile.size());
    const reader file = open_body(image_xml({}, R"(<ICCProfile compression="zlib:1024" location="inline:base64">)" +
                                                    openxisf::detail::encode_base64(block.data) + "</ICCProfile>"));
    EXPECT_TRUE(no_diagnostics(file.diagnostics()));
    EXPECT_EQ(file.image(0).icc_profile, profile);
}

TEST(conformance_icc, a_profile_is_big_endian_whatever_its_byte_order_attribute_says)
{
    // Spec §11.7: no byteOrder attribute; the bytes are the profile, a big-endian structure.
    const std::vector<std::byte> profile = profile_bytes();
    const reader file =
        open_attached(image_xml({}, R"(<ICCProfile byteOrder="little" location="attachment:{0}"/>)"), {profile});
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::invalid_byte_order,
                                  "/xisf/Image[1]/ICCProfile[1]"));
    EXPECT_EQ(file.image(0).icc_profile, profile);
}

TEST(conformance_icc, a_profile_without_a_data_block_is_unavailable)
{
    const reader file = open_body(image_xml({}, "<ICCProfile/>"));
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::error, errc::invalid_icc_profile,
                                  "/xisf/Image[1]/ICCProfile[1]"));
    EXPECT_TRUE(file.image(0).icc_profile.empty());
}

TEST(conformance_icc, a_profile_that_fails_its_checksum_is_unavailable_and_the_image_is_not)
{
    // An attached block is verified when it is loaded, which is when the unit opens.
    const reader file =
        open_attached(image_xml({}, R"(<ICCProfile checksum="sha1:0000000000000000000000000000000000000000" )"
                                    R"(location="attachment:{0}"/>)"),
                      {profile_bytes()});
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::error, errc::checksum_mismatch,
                                  "/xisf/Image[1]/ICCProfile[1]"));
    ASSERT_EQ(file.images().size(), 1U);
    EXPECT_TRUE(file.image(0).icc_profile.empty());

    EXPECT_TRUE(openxisf::test::throws<openxisf::integrity_error>(errc::checksum_mismatch, [] {
        (void)open_attached(image_xml({}, R"(<ICCProfile checksum="sha1:0000000000000000000000000000000000000000" )"
                                          R"(location="attachment:{0}"/>)"),
                            {profile_bytes()}, {.strict = true});
    }));
}

TEST(conformance_icc, an_image_has_one_profile)
{
    // The first one is the profile of the image; a second one is ignored.
    const std::vector<std::byte> second = bytes("second profile");
    const reader file =
        open_attached(image_xml({}, inline_profile() + R"(<ICCProfile location="attachment:{0}"/>)"), {second});
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::duplicate_element,
                                  "/xisf/Image[1]/ICCProfile[2]"));
    EXPECT_EQ(file.image(0).icc_profile, profile_bytes());
}

TEST(conformance_icc, a_profile_counts_against_the_limit_of_the_data_loaded_at_open)
{
    const std::vector<std::byte> profile = profile_bytes();
    const std::string body = image_xml({}, R"(<ICCProfile location="attachment:{0}"/>)");
    EXPECT_TRUE(no_diagnostics(open_attached(body, {profile}, {.limits = {.max_ancillary_data = 132}}).diagnostics()));
    const reader limited = open_attached(body, {profile}, {.limits = {.max_ancillary_data = 131}});
    EXPECT_TRUE(single_diagnostic(limited.diagnostics(), severity::error, errc::ancillary_data_too_large,
                                  "/xisf/Image[1]/ICCProfile[1]"));
    EXPECT_TRUE(limited.image(0).icc_profile.empty());
}

} // namespace
