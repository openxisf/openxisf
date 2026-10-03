// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include <openxisf/stream_io.h>

#include "support/bytes.h"
#include "support/faulty_io.h"
#include "support/files.h"
#include "support/temp_directory.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <ios>
#include <istream>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>

namespace {

using openxisf::errc;
using openxisf::io_error;
using openxisf::usage_error;
using openxisf::test::bytes;
using openxisf::test::c_file;
using openxisf::test::injected_fault;
using openxisf::test::read_file;
using openxisf::test::temp_directory;
using openxisf::test::text;
using openxisf::test::throws;
using openxisf::test::write_file;

// A buffer that reads only the first half of what is asked for, like a stream that ends early.
class short_buffer : public std::stringbuf
{
public:
    explicit short_buffer(const std::string& content) : std::stringbuf(content) {}

protected:
    std::streamsize xsgetn(char* destination, std::streamsize count) override
    {
        return std::stringbuf::xsgetn(destination, count / 2);
    }
};

// A buffer whose reads and writes throw, like a failing device. The stream catches the exception and sets badbit.
class failing_buffer : public std::stringbuf
{
public:
    failing_buffer() = default;
    explicit failing_buffer(const std::string& content) : std::stringbuf(content) {}

protected:
    std::streamsize xsgetn(char* /*destination*/, std::streamsize /*count*/) override
    {
        throw injected_fault();
    }

    std::streamsize xsputn(const char* /*source*/, std::streamsize /*count*/) override
    {
        throw injected_fault();
    }

    int sync() override
    {
        return -1;
    }
};

// A buffer that can only append, like a pipe.
class append_only_buffer : public std::streambuf
{
public:
    [[nodiscard]] const std::string& content() const noexcept
    {
        return content_;
    }

protected:
    int_type overflow(int_type c) override
    {
        if (!traits_type::eq_int_type(c, traits_type::eof())) {
            content_ += traits_type::to_char_type(c);
        }
        return traits_type::not_eof(c);
    }

private:
    std::string content_;
};

TEST(stdio_source, reads_from_its_start_position_to_the_end)
{
    const temp_directory directory;
    write_file(directory.path() / "data.bin", bytes("header0123456789"));
    const c_file file(directory.path() / "data.bin", "rb");
    ASSERT_EQ(std::fseek(file.get(), 6, SEEK_SET), 0);
    const openxisf::stdio_source source(file.get());
    std::array<std::byte, 4> destination{};

    EXPECT_EQ(source.size(), 10U);
    EXPECT_FALSE(source.supports_concurrent_reads());
    source.read(6, destination);
    EXPECT_EQ(text(destination), "6789");
    source.read(0, destination);
    EXPECT_EQ(text(destination), "0123");
    source.read(10, {});
    EXPECT_TRUE(throws<io_error>(errc::end_of_data, [&] { source.read(7, destination); }));
}

// The C library may hold a block of the file in its buffer, which a read at an offset outside that block bypasses. BSD
// libraries read the block that holds the target of a seek, so the constructor's seek to the end fills the buffer.
TEST(stdio_source, a_stream_that_shrinks_after_construction_is_end_of_data)
{
    const temp_directory directory;
    write_file(directory.path() / "data.bin", openxisf::test::pattern(std::size_t{1} << 20));
    const c_file file(directory.path() / "data.bin", "rb");
    const openxisf::stdio_source source(file.get());
    std::filesystem::resize_file(directory.path() / "data.bin", 1000);
    std::array<std::byte, 4> destination{};

    EXPECT_TRUE(throws<io_error>(errc::end_of_data, [&] { source.read(500'000, destination); }));
}

TEST(stdio_source, refuses_a_null_stream)
{
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [] { openxisf::stdio_source source(nullptr); }));
}

TEST(stdio_sink, writes_and_rewrites_from_its_start_position)
{
    const temp_directory directory;
    {
        const c_file file(directory.path() / "unit.xisf", "w+b");
        ASSERT_GE(std::fputs("prefix", file.get()), 0);
        openxisf::stdio_sink sink(file.get());

        EXPECT_TRUE(sink.can_rewrite());
        sink.write(bytes("0123456789"));
        sink.rewrite(2, bytes("ab"));
        sink.write(bytes("!"));
        EXPECT_EQ(sink.position(), 11U);
        EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [&] { sink.rewrite(10, bytes("xy")); }));
        sink.finish();
    }

    EXPECT_EQ(read_file(directory.path() / "unit.xisf"), "prefix01ab456789!");
}

TEST(stdio_sink, a_stream_that_cannot_be_written_is_write_failed)
{
    const temp_directory directory;
    write_file(directory.path() / "data.bin", bytes("read only"));
    const c_file file(directory.path() / "data.bin", "rb");
    openxisf::stdio_sink sink(file.get());

    EXPECT_TRUE(throws<io_error>(errc::write_failed, [&] { sink.write(bytes("abc")); }));
    EXPECT_EQ(sink.position(), 0U);
}

TEST(stdio_sink, refuses_a_null_stream)
{
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [] { openxisf::stdio_sink sink(nullptr); }));
}

TEST(istream_source, reads_from_its_start_position_to_the_end)
{
    std::istringstream stream("header0123456789");
    stream.seekg(6);
    const openxisf::istream_source source(stream);
    std::array<std::byte, 4> destination{};

    EXPECT_EQ(source.size(), 10U);
    EXPECT_FALSE(source.supports_concurrent_reads());
    source.read(6, destination);
    EXPECT_EQ(text(destination), "6789");
    source.read(0, destination);
    EXPECT_EQ(text(destination), "0123");
    EXPECT_TRUE(throws<io_error>(errc::end_of_data, [&] { source.read(7, destination); }));
}

TEST(istream_source, refuses_a_stream_that_cannot_seek)
{
    append_only_buffer buffer;
    std::istream stream(&buffer);

    EXPECT_TRUE(throws<io_error>(errc::not_seekable, [&] { openxisf::istream_source source(stream); }));
}

TEST(istream_source, a_stream_that_ends_early_is_end_of_data_and_one_that_fails_is_read_failed)
{
    short_buffer short_content("0123456789");
    std::istream ending(&short_content);
    const openxisf::istream_source ending_source(ending);
    failing_buffer failing_content("0123456789");
    std::istream failing(&failing_content);
    const openxisf::istream_source failing_source(failing);
    std::array<std::byte, 4> destination{};

    EXPECT_TRUE(throws<io_error>(errc::end_of_data, [&] { ending_source.read(0, destination); }));
    EXPECT_TRUE(throws<io_error>(errc::read_failed, [&] { failing_source.read(0, destination); }));
}

TEST(ostream_sink, writes_and_rewrites_from_its_start_position)
{
    std::ostringstream stream;
    stream << "prefix";
    openxisf::ostream_sink sink(stream);

    EXPECT_TRUE(sink.can_rewrite());
    sink.write(bytes("0123456789"));
    sink.rewrite(2, bytes("ab"));
    sink.write(bytes("!"));
    EXPECT_EQ(sink.position(), 11U);
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [&] { sink.rewrite(10, bytes("xy")); }));
    sink.finish();

    EXPECT_EQ(stream.str(), "prefix01ab456789!");
}

TEST(ostream_sink, cannot_rewrite_a_stream_that_cannot_seek)
{
    append_only_buffer buffer;
    std::ostream stream(&buffer);
    openxisf::ostream_sink sink(stream);

    sink.write(bytes("abc"));
    sink.finish();

    EXPECT_FALSE(sink.can_rewrite());
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [&] { sink.rewrite(0, bytes("x")); }));
    EXPECT_EQ(sink.position(), 3U);
    EXPECT_EQ(buffer.content(), "abc");
}

TEST(ostream_sink, a_stream_that_fails_is_write_failed)
{
    failing_buffer buffer;
    std::ostream stream(&buffer);
    openxisf::ostream_sink writing(stream);

    EXPECT_TRUE(throws<io_error>(errc::write_failed, [&] { writing.write(bytes("abc")); }));
    EXPECT_EQ(writing.position(), 0U);

    failing_buffer flushed;
    std::ostream flushing(&flushed);
    openxisf::ostream_sink finishing(flushing);
    EXPECT_TRUE(throws<io_error>(errc::write_failed, [&] { finishing.finish(); }));
}

} // namespace
