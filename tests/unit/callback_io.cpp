// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <openxisf/error.h>
#include <openxisf/io.h>

#include "support/bytes.h"
#include "support/faulty_io.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using openxisf::callback_sink;
using openxisf::callback_source;
using openxisf::errc;
using openxisf::io_error;
using openxisf::usage_error;
using openxisf::test::bytes;
using openxisf::test::injected_fault;
using openxisf::test::pattern;
using openxisf::test::text;
using openxisf::test::throws;

TEST(callback_source, reads_through_its_function)
{
    const std::vector<std::byte> data = pattern(100);
    std::vector<std::pair<std::uint64_t, std::size_t>> calls;
    const callback_source source(data.size(), [&](std::uint64_t offset, std::span<std::byte> destination) {
        calls.emplace_back(offset, destination.size());
        std::ranges::copy(std::span(data).subspan(offset, destination.size()), destination.begin());
    });

    std::array<std::byte, 10> destination{};
    source.read(90, destination);

    EXPECT_EQ(source.size(), 100U);
    EXPECT_TRUE(std::ranges::equal(destination, std::span(data).subspan(90)));
    EXPECT_EQ(calls, (std::vector<std::pair<std::uint64_t, std::size_t>>{{90, 10}}));
}

TEST(callback_source, never_asks_its_function_for_bytes_beyond_the_size)
{
    int calls = 0;
    const callback_source source(100, [&](std::uint64_t, std::span<std::byte>) { ++calls; },
                                 {.description = "the archive entry"});
    std::array<std::byte, 10> destination{};

    try {
        source.read(91, destination);
        ADD_FAILURE() << "no exception";
    } catch (const io_error& failure) {
        EXPECT_EQ(failure.code(), errc::end_of_data);
        EXPECT_STREQ(failure.what(),
                     "a read of 10 bytes goes past the end of the archive entry, which has 100 bytes (offset 91)");
    }
    EXPECT_EQ(calls, 0);
}

TEST(callback_source, exceptions_of_its_function_pass_through)
{
    const callback_source source(10, [](std::uint64_t, std::span<std::byte>) { throw injected_fault(); });
    std::array<std::byte, 1> destination{};

    EXPECT_THROW(source.read(0, destination), injected_fault);
}

TEST(callback_source, reports_its_options)
{
    const auto read = [](std::uint64_t, std::span<std::byte>) {};
    const callback_source plain(0, read);
    const callback_source concurrent(0, read, {.concurrent_reads = true, .description = "entry"});

    EXPECT_FALSE(plain.supports_concurrent_reads());
    EXPECT_EQ(plain.description(), "");
    EXPECT_TRUE(concurrent.supports_concurrent_reads());
    EXPECT_EQ(concurrent.description(), "entry");
}

TEST(callback_source, needs_a_read_function)
{
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [] { callback_source source(0, {}); }));
}

TEST(callback_sink, writes_through_its_function_and_counts_the_bytes)
{
    std::string written;
    callback_sink sink([&](std::span<const std::byte> data) { written += text(data); });

    sink.write(bytes("abc"));
    sink.write(bytes("de"));
    sink.finish();

    EXPECT_EQ(written, "abcde");
    EXPECT_EQ(sink.position(), 5U);
}

TEST(callback_sink, rewrites_only_through_a_rewrite_function)
{
    std::string written;
    const auto write = [&](std::span<const std::byte> data) { written += text(data); };
    callback_sink plain(write);
    callback_sink rewriting(write, [&](std::uint64_t offset, std::span<const std::byte> data) {
        written.replace(offset, data.size(), text(data));
    });
    rewriting.write(bytes("0123456789"));

    EXPECT_FALSE(plain.can_rewrite());
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [&] { plain.rewrite(0, {}); }));
    EXPECT_TRUE(rewriting.can_rewrite());
    rewriting.rewrite(2, bytes("ab"));
    EXPECT_EQ(written, "01ab456789");
    // The function never sees a rewrite beyond the bytes written.
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [&] { rewriting.rewrite(9, bytes("xy")); }));
    EXPECT_EQ(written, "01ab456789");
}

TEST(callback_sink, finish_calls_the_finish_function)
{
    int finished = 0;
    callback_sink sink([](std::span<const std::byte>) {}, {}, [&] { ++finished; });

    sink.finish();

    EXPECT_EQ(finished, 1);
}

TEST(callback_sink, exceptions_of_its_functions_pass_through_and_a_failed_write_is_not_counted)
{
    const auto fail = [](std::span<const std::byte>) { throw injected_fault(); };
    callback_sink sink(
        fail, [](std::uint64_t, std::span<const std::byte>) { throw injected_fault(); },
        [] { throw injected_fault(); });

    EXPECT_THROW(sink.write(bytes("abc")), injected_fault);
    EXPECT_EQ(sink.position(), 0U);
    EXPECT_THROW(sink.rewrite(0, {}), injected_fault);
    EXPECT_THROW(sink.finish(), injected_fault);
}

TEST(callback_sink, needs_a_write_function)
{
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [] { callback_sink sink({}); }));
}

} // namespace
