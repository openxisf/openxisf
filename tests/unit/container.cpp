// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <openxisf/error.h>
#include <openxisf/io.h>
#include <openxisf/limits.h>

#include "container/file_layout.h"
#include "core/diagnostic_log.h"
#include "io/thread_safe_source.h"
#include "support/bytes.h"
#include "support/diagnostics.h"
#include "support/faulty_io.h"
#include "support/fixture_builder.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace std::string_view_literals;
using openxisf::errc;
using openxisf::severity;
using openxisf::unit_storage;
using openxisf::detail::diagnostic_log;
using openxisf::detail::read_unit_header;
using openxisf::detail::thread_safe_source;
using openxisf::detail::unit_header;
using openxisf::test::bytes;
using openxisf::test::file_preamble;
using openxisf::test::header_xml;
using openxisf::test::monolithic_file;
using openxisf::test::no_diagnostics;
using openxisf::test::single_diagnostic;
using openxisf::test::throws;

struct read_result
{
    unit_header header{};
    std::vector<openxisf::diagnostic> diagnostics{};
};

read_result read(std::vector<std::byte> unit, const openxisf::limits& limits = {})
{
    const thread_safe_source source(std::make_unique<openxisf::memory_source>(std::move(unit)));
    diagnostic_log log(false);
    unit_header header = read_unit_header(source, limits, log);
    return {.header = std::move(header), .diagnostics = log.release()};
}

testing::AssertionResult refuses(errc code, std::vector<std::byte> unit)
{
    return throws<openxisf::invalid_data_error>(code, [&unit] { (void)read(std::move(unit)); });
}

// ---------------------------------------------------------------------------------------------------------------------
// Spec §9.2: monolithic files

TEST(container, reads_the_header_of_a_monolithic_file)
{
    const std::string header = header_xml();
    std::vector<std::byte> file = monolithic_file(header);
    // The header length ends the header, not the end of the file.
    const std::vector<std::byte> block = bytes("an attached block");
    file.insert(file.end(), block.begin(), block.end());

    const auto [found, diagnostics] = read(file);

    EXPECT_EQ(found.storage, unit_storage::monolithic);
    EXPECT_EQ(found.offset, 16U);
    EXPECT_EQ(found.end, 16U + header.size());
    EXPECT_EQ(found.text, header);
    EXPECT_TRUE(no_diagnostics(diagnostics));
}

// Spec §8.2: the integers of the file structure are little-endian.
TEST(container, the_header_length_is_little_endian)
{
    // 0x0201 bytes, which would be 0x01020000 in big-endian order, beyond the end of the file.
    const std::string header(0x0201, 'h');
    std::vector<std::byte> file = bytes("XISF0100");
    for (const unsigned int value : {0x01U, 0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U}) {
        file.push_back(static_cast<std::byte>(value));
    }
    const std::vector<std::byte> text = bytes(header);
    file.insert(file.end(), text.begin(), text.end());

    EXPECT_EQ(read(file).header.text, header);
}

TEST(container, a_header_length_of_at_least_65_bytes_is_accepted)
{
    // The shortest header from <?xml to </xisf>.
    const std::string header(65, 'h');
    EXPECT_EQ(read(monolithic_file(header)).header.text, header);
}

TEST(container, refuses_a_header_length_below_65_bytes)
{
    const std::string header(100, 'h');
    for (const std::uint32_t length : {0U, 1U, 64U}) {
        EXPECT_TRUE(refuses(errc::invalid_header_length, monolithic_file(header, {.header_length = length}))) << length;
    }
}

TEST(container, refuses_a_header_length_beyond_the_end_of_the_file)
{
    const std::string header(100, 'h');
    for (const std::uint32_t length : {101U, 0xFFFFFFFFU}) {
        try {
            (void)read(monolithic_file(header, {.header_length = length}));
            ADD_FAILURE() << length;
        } catch (const openxisf::invalid_data_error& failure) {
            EXPECT_EQ(failure.code(), errc::invalid_header_length);
            EXPECT_EQ(failure.context().offset, 8U);
        }
    }
}

TEST(container, refuses_a_file_that_ends_before_its_header_length)
{
    EXPECT_TRUE(refuses(errc::invalid_header_length, bytes("XISF0100")));
    EXPECT_TRUE(refuses(errc::invalid_header_length, bytes("XISF0100\x41\0\0\0\0\0\0"sv)));
}

TEST(container, tolerates_a_reserved_field_that_is_not_zero)
{
    const std::string header = header_xml();
    for (const std::uint32_t reserved : {0x01U, 0x0100U, 0x010000U, 0x01000000U}) {
        const auto [found, diagnostics] = read(monolithic_file(header, {.reserved = reserved}));
        EXPECT_EQ(found.text, header);
        ASSERT_TRUE(single_diagnostic(diagnostics, severity::warning, errc::reserved_field_not_zero)) << reserved;
        EXPECT_EQ(diagnostics.front().context.offset, 12U);
    }
}

TEST(container, ignores_zero_bytes_that_the_header_length_counts_after_the_xml)
{
    const std::string header = header_xml();
    const auto [found, diagnostics] = read(monolithic_file(header + std::string(10, '\0')));

    EXPECT_EQ(found.text, header);
    // Attached blocks still start after the zero bytes.
    EXPECT_EQ(found.end, 16U + header.size() + 10U);
    ASSERT_TRUE(single_diagnostic(diagnostics, severity::warning, errc::invalid_header_length));
    EXPECT_EQ(diagnostics.front().context.offset, 8U);
}

TEST(container, refuses_a_header_larger_than_the_limit)
{
    const std::string header(100, 'h');
    EXPECT_EQ(read(monolithic_file(header), {.max_header_size = 100}).header.text, header);
    EXPECT_EQ(read(monolithic_file(header), {.max_header_size = 0}).header.text, header);
    EXPECT_TRUE(throws<openxisf::limit_error>(
        errc::header_too_large, [&header] { (void)read(monolithic_file(header), {.max_header_size = 99}); }));
}

// ---------------------------------------------------------------------------------------------------------------------
// Spec §9.3: header files, recognized by their content

TEST(container, a_source_that_starts_with_xml_is_a_header_file)
{
    // XML may start with a byte order mark, and with white space when it has no XML declaration.
    for (const std::string_view start : {"", "\xEF\xBB\xBF", "\r\n \t"}) {
        const std::string text = std::string(start) + header_xml();
        const auto [found, diagnostics] = read(bytes(text));

        EXPECT_EQ(found.storage, unit_storage::distributed);
        EXPECT_EQ(found.offset, 0U);
        EXPECT_EQ(found.end, text.size());
        EXPECT_EQ(found.text, text);
        EXPECT_TRUE(no_diagnostics(diagnostics));
    }
}

TEST(container, refuses_a_header_file_larger_than_the_limit)
{
    const std::string text = header_xml();
    EXPECT_TRUE(throws<openxisf::limit_error>(
        errc::header_too_large, [&text] { (void)read(bytes(text), {.max_header_size = text.size() - 1}); }));
}

TEST(container, refuses_a_source_that_is_not_a_unit)
{
    const std::string header = header_xml();
    const std::vector<std::string> sources{
        "",
        "XISF0101" + header,               // another version
        "xisf0100" + header,               // the signature is case-sensitive
        "XISF010",                         // a signature cut short
        "XISB0100" + std::string(8, '\0'), // a data blocks file (spec §9.4)
        "\x89PNG\r\n\x1A\n",               // another format
        "SIMPLE  =                    T",  // a FITS file
        "\xEF\xBB\xBF",                    // a byte order mark followed by nothing
        "\xEF\xBB\xBFXISF0100" + header,   // a byte order mark before the signature
    };
    for (const std::string& source : sources) {
        EXPECT_TRUE(refuses(errc::not_an_xisf_unit, bytes(source))) << source.substr(0, 16);
    }
}

TEST(container, failures_of_the_source_pass_through)
{
    const openxisf::memory_source unit(monolithic_file(header_xml()));
    // The first read takes the first 16 bytes, the second the header.
    for (const std::size_t failing_read : {1U, 2U}) {
        EXPECT_TRUE(throws<openxisf::io_error>(errc::read_failed, [&] {
            const thread_safe_source source(
                std::make_unique<openxisf::test::faulty_source>(unit, failing_read, openxisf::test::fault::error));
            diagnostic_log log(false);
            (void)read_unit_header(source, {}, log);
        })) << failing_read;
    }
}

} // namespace
