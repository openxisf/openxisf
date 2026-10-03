// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

// Spec §10.4: the byte order of data blocks, on constructed units: recorded with the block, and applied to the samples
// of images when their pixels are read.

#include <openxisf/error.h>
#include <openxisf/reader.h>

#include "container/block_attributes.h"
#include "model/unit.h"
#include "support/bytes.h"
#include "support/diagnostics.h"
#include "support/fixture_builder.h"
#include "support/opened_unit.h"

#include <gtest/gtest.h>

#include <bit>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

using openxisf::errc;
using openxisf::severity;
using openxisf::detail::byte_order;
using openxisf::detail::data_block;
using openxisf::detail::unit;
using openxisf::test::block_at;
using openxisf::test::header_xml;
using openxisf::test::monolithic_file;
using openxisf::test::no_diagnostics;
using openxisf::test::open_internal;
using openxisf::test::single_diagnostic;

unit open_body(std::string_view body)
{
    return open_internal(monolithic_file(header_xml(body)));
}

// A gray 16-bit image of two pixels in an embedded block of hexadecimal data, whose Data element has the given
// attributes.
std::string image(std::string_view data_attributes)
{
    return R"(<Image geometry="2:1:1" sampleFormat="UInt16" colorSpace="Gray" location="embedded">)"
           R"(<Data encoding="hex" )" +
           std::string(data_attributes) + ">01020304</Data></Image>";
}

// The byte order of the block at path, or nothing when it is unavailable.
std::optional<byte_order> order_of(const unit& opened, std::string_view path)
{
    const data_block& block = block_at(opened, path);
    return block.descriptor ? std::optional(block.descriptor->order) : std::nullopt;
}

TEST(conformance_byte_order, a_data_block_is_little_endian_by_default)
{
    const unit opened = open_body(image({}));
    EXPECT_TRUE(no_diagnostics(opened.diagnostics));
    EXPECT_EQ(order_of(opened, "/xisf/Image[1]"), byte_order::little);
}

TEST(conformance_byte_order, a_data_block_can_be_big_or_little_endian)
{
    const unit opened = open_body(
        R"(<Property id="Test:Big" type="UI16Vector" length="2" location="inline:hex" byteOrder="big">01020304</Property>)"
        R"(<Property id="Test:Little" type="UI16Vector" length="2" location="inline:hex" byteOrder="little">)"
        R"(01020304</Property>)");
    EXPECT_TRUE(no_diagnostics(opened.diagnostics));
    EXPECT_EQ(order_of(opened, "/xisf/Property[1]"), byte_order::big);
    EXPECT_EQ(order_of(opened, "/xisf/Property[2]"), byte_order::little);
    // The stored bytes are not changed: the byte order says how to read them.
    EXPECT_EQ(openxisf::test::stored_block(opened, "/xisf/Property[1]"), openxisf::test::bytes("\x01\x02\x03\x04"));
}

TEST(conformance_byte_order, the_byte_order_of_an_embedded_block_is_on_its_data_element)
{
    const unit opened = open_body(image(R"(byteOrder="big")"));
    EXPECT_TRUE(no_diagnostics(opened.diagnostics));
    EXPECT_EQ(order_of(opened, "/xisf/Image[1]"), byte_order::big);
}

// The scalars of samples, as unsigned integers of their size: the real part, then the imaginary part, of a complex
// sample.
template <typename T> std::vector<std::uint64_t> scalars_of(const std::vector<T>& samples)
{
    std::vector<std::uint64_t> scalars;
    for (const T& sample : samples) {
        if constexpr (std::is_same_v<T, float>) {
            scalars.push_back(std::bit_cast<std::uint32_t>(sample));
        } else if constexpr (std::is_same_v<T, double>) {
            scalars.push_back(std::bit_cast<std::uint64_t>(sample));
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            scalars.push_back(std::bit_cast<std::uint32_t>(sample.real()));
            scalars.push_back(std::bit_cast<std::uint32_t>(sample.imag()));
        } else if constexpr (std::is_same_v<T, std::complex<double>>) {
            scalars.push_back(std::bit_cast<std::uint64_t>(sample.real()));
            scalars.push_back(std::bit_cast<std::uint64_t>(sample.imag()));
        } else {
            scalars.push_back(sample);
        }
    }
    return scalars;
}

// The hexadecimal digits of samples in the given byte order, written out from the bits of their scalars.
template <typename T> std::string hex_of(const std::vector<T>& samples, bool big)
{
    constexpr std::size_t size =
        std::is_same_v<T, std::complex<float>> || std::is_same_v<T, std::complex<double>> ? sizeof(T) / 2 : sizeof(T);
    constexpr std::string_view digits = "0123456789abcdef";
    std::string hex;
    for (const std::uint64_t scalar : scalars_of(samples)) {
        for (std::size_t i = 0; i < size; ++i) {
            const std::size_t shift = 8 * (big ? size - 1 - i : i);
            hex += digits[(scalar >> (shift + 4)) & 0xFU];
            hex += digits[(scalar >> shift) & 0xFU];
        }
    }
    return hex;
}

// An image of the given sample format reads samples from a block of either byte order.
template <typename T> void expect_both_byte_orders(std::string_view format, const std::vector<T>& samples)
{
    for (const bool big : {true, false}) {
        const std::string header =
            header_xml(R"(<Image geometry=")" + std::to_string(samples.size()) + R"(:1:1" sampleFormat=")" +
                       std::string(format) + R"(" bounds="0:1" location="embedded"><Data encoding="hex" byteOrder=")" +
                       (big ? "big" : "little") + R"(">)" + hex_of(samples, big) + "</Data></Image>");
        const openxisf::reader file = openxisf::test::open_header(header);
        EXPECT_TRUE(no_diagnostics(file.diagnostics())) << format;
        EXPECT_EQ(file.read_pixels<T>(0), samples) << format << (big ? " big" : " little");
    }
}

TEST(conformance_byte_order, the_samples_of_every_multi_byte_format_are_read_from_either_byte_order)
{
    // The values have a different byte in every position, so that a wrong order shows.
    expect_both_byte_orders<std::uint16_t>("UInt16", {0x0102, 0xA1B2});
    expect_both_byte_orders<std::uint32_t>("UInt32", {0x01020304, 0xA1B2C3D4});
    expect_both_byte_orders<std::uint64_t>("UInt64", {0x0102030405060708, 0xA1B2C3D4E5F60718});
    expect_both_byte_orders<float>("Float32", {1.5F, -2.25e-3F});
    expect_both_byte_orders<double>("Float64", {1e300, -0.1});
    expect_both_byte_orders<std::complex<float>>("Complex32", {{1.5F, -2.0F}, {3e-5F, 7.0F}});
    expect_both_byte_orders<std::complex<double>>("Complex64", {{1e-10, 3.0}, {-1.0, 2.5}});
}

TEST(conformance_byte_order, an_icc_profile_has_no_byte_order)
{
    // ICC profiles are big-endian structures, kept unaltered (spec §11.7). The attribute is ignored with a warning.
    const unit opened = open_body(R"(<ICCProfile location="inline:hex" byteOrder="little">01020304</ICCProfile>)");
    EXPECT_TRUE(
        single_diagnostic(opened.diagnostics, severity::warning, errc::invalid_byte_order, "/xisf/ICCProfile[1]"));
    EXPECT_EQ(openxisf::test::stored_block(opened, "/xisf/ICCProfile[1]"), openxisf::test::bytes("\x01\x02\x03\x04"));
}

} // namespace
