// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Spec §10.4: the byte order of data blocks, on constructed units: recorded with the block, and applied to the samples
// of images when their pixels are read, whatever the way of reading them and the storage model asked for.

#include <openxisf/error.h>
#include <openxisf/reader.h>
#include <openxisf/types.h>

#include "container/block_attributes.h"
#include "core/data_encoding.h"
#include "model/unit.h"
#include "support/bytes.h"
#include "support/diagnostics.h"
#include "support/fixture_builder.h"
#include "support/opened_unit.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

using openxisf::errc;
using openxisf::pixel_storage;
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

// How the pixel data of an image are stored: their storage model and byte order.
struct stored_layout
{
    pixel_storage storage = pixel_storage::planar;
    bool big = false;
};

std::string_view name_of(pixel_storage storage)
{
    return storage == pixel_storage::planar ? "planar" : "normal";
}

// The layout: CTest names the tests after it. The name is fixed by GoogleTest.
// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const stored_layout& value, std::ostream* output)
{
    *output << name_of(value.storage) << (value.big ? "_big" : "_little");
}

// The samples of image 0 read in each way that the reader offers, which take different paths: into a vector of bytes,
// into bytes of the caller, and as samples, into a vector and into samples of the caller.
template <typename T>
std::vector<std::vector<T>> read_each_way(const openxisf::reader& file, const openxisf::pixel_read_options& options)
{
    const std::size_t count = file.image(0).geometry.sample_count();
    std::vector<std::vector<T>> reads(4, std::vector<T>(count));
    const std::vector<std::byte> bytes = file.read_pixels(0, options);
    const std::span<std::byte> first = std::as_writable_bytes(std::span(reads[0]));
    EXPECT_EQ(bytes.size(), first.size());
    if (bytes.size() == first.size()) {
        std::ranges::copy(bytes, first.begin());
    }
    file.read_pixels(0, std::as_writable_bytes(std::span(reads[1])), options);
    reads[2] = file.read_pixels<T>(0, options);
    file.read_pixels(0, std::span<T>(reads[3]), options);
    return reads;
}

// An RGB image of two pixels of the given sample format, whose samples are given in planar order (channel c of pixel p
// at 2c + p) and stored with the given layout, reads them in native byte order each way, in the storage model of the
// image and in each one asked for.
template <typename T>
void expect_every_read(std::string_view format, const std::vector<T>& planar, const stored_layout& layout)
{
    const std::vector<T> normal{planar[0], planar[2], planar[4], planar[1], planar[3], planar[5]};
    const bool stored_planar = layout.storage == pixel_storage::planar;
    const std::string header = header_xml(
        R"(<Image geometry="2:1:3" sampleFormat=")" + std::string(format) + R"(" colorSpace="RGB" pixelStorage=")" +
        (stored_planar ? "Planar" : "Normal") +
        R"(" bounds="0:1" location="embedded"><Data encoding="hex" byteOrder=")" + (layout.big ? "big" : "little") +
        R"(">)" + hex_of(stored_planar ? planar : normal, layout.big) + "</Data></Image>");
    const openxisf::reader file = openxisf::test::open_header(header);
    EXPECT_TRUE(no_diagnostics(file.diagnostics())) << format;
    for (const std::optional<pixel_storage> asked :
         {std::optional<pixel_storage>{}, std::optional(pixel_storage::planar), std::optional(pixel_storage::normal)}) {
        const std::vector<T>& expected = asked.value_or(layout.storage) == pixel_storage::planar ? planar : normal;
        const std::vector<std::vector<T>> reads = read_each_way<T>(file, {.storage = asked});
        for (std::size_t way = 0; way < reads.size(); ++way) {
            EXPECT_EQ(reads[way], expected)
                << format << ", read " << way << ", in " << (asked ? name_of(*asked) : "the storage of the image");
        }
    }
}

class conformance_byte_order_layout : public testing::TestWithParam<stored_layout>
{};

TEST_P(conformance_byte_order_layout, every_multi_byte_format_reads_in_native_byte_order_every_way)
{
    // Spec §8.5.3, §10.4: sample format × storage model × byte order. The values have a different byte in most
    // positions, so that a wrong order shows.
    expect_every_read<std::uint16_t>("UInt16", {0x0102, 0xA1B2, 0x1324, 0xB3C4, 0x2536, 0xC5D6}, GetParam());
    expect_every_read<std::uint32_t>("UInt32", {0x01020304, 0xA1B2C3D4, 0x11223344, 0xB1C2D3E4, 0x21324354, 0xC1D2E3F4},
                                     GetParam());
    expect_every_read<std::uint64_t>("UInt64",
                                     {0x0102030405060708, 0xA1B2C3D4E5F60718, 0x1122334455667788, 0xB1C2D3E4F5061728,
                                      0x2132435465768798, 0xC1D2E3F405162738},
                                     GetParam());
    expect_every_read<float>("Float32", {1.5F, -2.25e-3F, 3.1e10F, -7.25F, 0.1F, 6.5e-20F}, GetParam());
    expect_every_read<double>("Float64", {1e300, -0.1, 2.5e-200, -7.125, 0.3, 1234.5678}, GetParam());
    expect_every_read<std::complex<float>>(
        "Complex32", {{1.5F, -2.0F}, {3e-5F, 7.0F}, {-0.1F, 9.5F}, {4.25F, -3e3F}, {0.7F, 0.2F}, {-6.0F, 1e-7F}},
        GetParam());
    expect_every_read<std::complex<double>>(
        "Complex64", {{1e-10, 3.0}, {-1.0, 2.5}, {0.1, -7e100}, {4.5, 0.3}, {-2e-300, 6.0}, {8.25, -0.7}}, GetParam());
}

INSTANTIATE_TEST_SUITE_P(layouts, conformance_byte_order_layout,
                         testing::Values(stored_layout{.storage = pixel_storage::planar, .big = true},
                                         stored_layout{.storage = pixel_storage::planar, .big = false},
                                         stored_layout{.storage = pixel_storage::normal, .big = true},
                                         stored_layout{.storage = pixel_storage::normal, .big = false}),
                         [](const testing::TestParamInfo<stored_layout>& parameter) {
                             return testing::PrintToString(parameter.param);
                         });

TEST(conformance_byte_order, an_icc_profile_has_no_byte_order)
{
    // ICC profiles are big-endian structures, kept unaltered (spec §11.7). The attribute is ignored with a warning.
    const std::vector<std::byte> profile = openxisf::test::icc_profile();
    const unit opened = open_body(R"(<ICCProfile location="inline:hex" byteOrder="little">)" +
                                  openxisf::detail::encode_hex(profile) + "</ICCProfile>");
    EXPECT_TRUE(
        single_diagnostic(opened.diagnostics, severity::warning, errc::invalid_byte_order, "/xisf/ICCProfile[1]"));
    EXPECT_EQ(openxisf::test::stored_block(opened, "/xisf/ICCProfile[1]"), profile);
}

} // namespace
