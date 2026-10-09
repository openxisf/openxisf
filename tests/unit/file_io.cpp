// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <openxisf/error.h>
#include <openxisf/io.h>

#include "support/bytes.h"
#include "support/faulty_io.h"
#include "support/files.h"
#include "support/links.h"
#include "support/sparse_file.h"
#include "support/temp_directory.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using openxisf::errc;
using openxisf::file_sink;
using openxisf::file_source;
using openxisf::io_error;
using openxisf::usage_error;
using openxisf::test::bytes;
using openxisf::test::pattern;
using openxisf::test::read_file;
using openxisf::test::temp_directory;
using openxisf::test::text;
using openxisf::test::throws;
using openxisf::test::utf8;
using openxisf::test::working_directory;
using openxisf::test::write_file;

// The UTF-8 names of the entries of a directory, sorted.
std::vector<std::string> entries(const std::filesystem::path& directory)
{
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        names.push_back(utf8(entry.path().filename()));
    }
    std::ranges::sort(names);
    return names;
}

std::vector<std::byte> read_all(const openxisf::input_source& source)
{
    std::vector<std::byte> data(source.size());
    source.read(0, data);
    return data;
}

void write_through_sink(const std::string& path, std::string_view content)
{
    file_sink sink(path);
    sink.write(bytes(content));
    sink.finish();
}

TEST(file_source, reads_every_range_of_a_file)
{
    const temp_directory directory;
    const std::vector<std::byte> data = pattern(37);
    write_file(directory.path() / "data.bin", data);
    const file_source source(directory.file("data.bin"));

    ASSERT_EQ(source.size(), data.size());
    for (std::size_t offset = 0; offset <= data.size(); ++offset) {
        for (std::size_t length = 0; offset + length <= data.size(); ++length) {
            std::vector<std::byte> destination(length);
            source.read(offset, destination);
            ASSERT_TRUE(std::ranges::equal(destination, std::span(data).subspan(offset, length)))
                << offset << ' ' << length;
        }
    }
}

TEST(file_source, reads_past_the_end_are_end_of_data)
{
    const temp_directory directory;
    write_file(directory.path() / "data.bin", bytes("0123456789"));
    const file_source source(directory.file("data.bin"));
    std::array<std::byte, 4> destination{};

    EXPECT_TRUE(throws<io_error>(errc::end_of_data, [&] { source.read(7, destination); }));
    EXPECT_TRUE(throws<io_error>(errc::end_of_data, [&] { source.read(11, {}); }));
}

TEST(file_source, empty_file)
{
    const temp_directory directory;
    write_file(directory.path() / "empty.bin", {});
    const file_source source(directory.file("empty.bin"));
    std::array<std::byte, 1> destination{};

    EXPECT_EQ(source.size(), 0U);
    source.read(0, {});
    EXPECT_TRUE(throws<io_error>(errc::end_of_data, [&] { source.read(0, destination); }));
}

TEST(file_source, describes_itself_with_its_path_and_supports_concurrent_reads)
{
    const temp_directory directory;
    write_file(directory.path() / "data.bin", bytes("abc"));
    const std::string path = directory.file("data.bin");
    const file_source source(path);

    EXPECT_EQ(source.description(), path);
    EXPECT_TRUE(source.supports_concurrent_reads());
}

TEST(file_source, a_file_that_shrinks_after_opening_is_end_of_data)
{
    const temp_directory directory;
    write_file(directory.path() / "data.bin", pattern(100));
    const file_source source(directory.file("data.bin"));
    std::filesystem::resize_file(directory.path() / "data.bin", 50);
    std::array<std::byte, 20> destination{};

    source.read(30, destination);
    EXPECT_TRUE(throws<io_error>(errc::end_of_data, [&] { source.read(40, destination); }));
}

TEST(file_source, reads_beyond_4_gib)
{
    const temp_directory directory;
    const std::filesystem::path path = directory.path() / "large.bin";
    const std::uint64_t boundary = std::uint64_t{1} << 32;
    const std::uint64_t size = (std::uint64_t{5} << 30) + 3;
    if (!openxisf::test::create_sparse_file(path, size)) {
        GTEST_SKIP() << "the file system cannot create sparse files";
    }
    openxisf::test::write_at(path, boundary - 4, bytes("ABCDEFGH"));
    openxisf::test::write_at(path, size - 3, bytes("xyz"));
    const file_source source(utf8(path));
    std::array<std::byte, 8> across{};
    std::array<std::byte, 4> beyond{};
    std::array<std::byte, 3> end{};

    EXPECT_EQ(source.size(), size);
    source.read(boundary - 4, across);
    source.read(boundary, beyond);
    source.read(size - 3, end);
    EXPECT_EQ(text(across), "ABCDEFGH");
    EXPECT_EQ(text(beyond), "EFGH");
    EXPECT_EQ(text(end), "xyz");
}

TEST(file_source, reads_more_than_1_gib_in_one_call)
{
    // Windows transfers at most 1 GiB in a call of the system, and POSIX systems may transfer less than asked for, so
    // one read of 1 GiB and a byte takes several.
    const temp_directory directory;
    const std::filesystem::path path = directory.path() / "large.bin";
    constexpr std::size_t gib = std::size_t{1} << 30;
    if (!openxisf::test::create_sparse_file(path, gib + 1)) {
        GTEST_SKIP() << "the file system cannot create sparse files";
    }
    openxisf::test::write_at(path, 0, bytes("AB"));
    openxisf::test::write_at(path, gib - 2, bytes("CDE"));
    const file_source source(utf8(path));
    std::vector<std::byte> data(gib + 1, std::byte{0xEE});

    source.read(0, data);
    EXPECT_EQ(text(std::span(data).first(2)), "AB");
    EXPECT_EQ(text(std::span(data).last(3)), "CDE");
    // Every byte between them was read: the zeros of the file replaced the bytes of the buffer.
    EXPECT_TRUE(std::ranges::find(data, std::byte{0xEE}) == data.end());
}

TEST(file_source, a_missing_file_is_open_failed)
{
    const temp_directory directory;

    try {
        const file_source source(directory.file("missing.xisf"));
        ADD_FAILURE() << "no exception";
    } catch (const io_error& failure) {
        EXPECT_EQ(failure.code(), errc::open_failed);
        EXPECT_EQ(failure.system_code(), std::errc::no_such_file_or_directory);
        EXPECT_NE(std::string_view(failure.what()).find(directory.file("missing.xisf")), std::string_view::npos);
    }
}

TEST(file_source, refuses_directories_and_devices)
{
    const temp_directory directory;
#if defined(_WIN32)
    const std::string device = "NUL";
#else
    const std::string device = "/dev/null";
#endif

    EXPECT_TRUE(throws<io_error>(errc::not_a_regular_file, [&] { file_source source(utf8(directory.path())); }));
    EXPECT_TRUE(throws<io_error>(errc::not_a_regular_file, [&] { file_source source(device); }));
}

TEST(file_source, refuses_empty_and_invalid_utf8_paths)
{
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [] { file_source source(""); }));
    EXPECT_TRUE(throws<usage_error>(errc::invalid_utf8, [] { file_source source("a\xC3.xisf"); }));
    EXPECT_TRUE(throws<usage_error>(errc::invalid_utf8, [] { file_source source(std::string_view("a\0b", 3)); }));
}

// A file name, and the name of its test.
struct named_file
{
    std::string_view test{};
    std::string_view file{};
};

// GoogleTest prints the parameter in messages, and CTest names the test after it. The name is fixed by GoogleTest.
// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const named_file& value, std::ostream* out)
{
    *out << value.test;
}

class non_ascii_path : public testing::TestWithParam<named_file>
{};

// The name is checked through std::filesystem, so a path converted wrongly on the way to the system shows as a missing
// entry, not as a file that the library alone can find again.
TEST_P(non_ascii_path, is_written_and_read_under_its_name)
{
    const temp_directory directory;
    const std::string path = directory.file(GetParam().file);

    write_through_sink(path, "content");
    const file_source source(path);

    EXPECT_EQ(text(read_all(source)), "content");
    EXPECT_EQ(entries(directory.path()), std::vector<std::string>{std::string(GetParam().file)});
}

// Characters outside the BMP take surrogate pairs in UTF-16.
INSTANTIATE_TEST_SUITE_P(file_io, non_ascii_path,
                         testing::Values(named_file{.test = "latin", .file = "Nebulosa del Ñandú.xisf"},
                                         named_file{.test = "cjk", .file = "星雲.xisf"},
                                         named_file{.test = "outside_the_bmp", .file = "𝔛 😀.xisf"}),
                         [](const testing::TestParamInfo<named_file>& instance) {
                             return std::string(instance.param.test);
                         });

TEST(file_io, paths_longer_than_260_characters)
{
    const temp_directory directory;
    const std::filesystem::path folder = directory.path() / std::string(100, 'd') / std::string(100, 'e');
    std::filesystem::create_directory(openxisf::test::long_form(folder.parent_path()));
    std::filesystem::create_directory(openxisf::test::long_form(folder));
    const std::string path = utf8(folder / (std::string(100, 'f') + ".xisf"));
    ASSERT_GT(path.size(), 300U);

    write_through_sink(path, "content");
    const file_source source(path);

    EXPECT_EQ(text(read_all(source)), "content");
}

TEST(file_sink, a_relative_target_is_taken_from_the_current_directory_when_the_sink_is_made)
{
    // The sink replaces the file that the path named when it was made, and leaves nothing in the current directory of
    // its finish().
    const temp_directory directory;
    std::filesystem::create_directory(directory.path() / "made");
    std::filesystem::create_directory(directory.path() / "finished");
    std::unique_ptr<file_sink> sink;
    {
        const working_directory moved(directory.path() / "made");
        sink = std::make_unique<file_sink>("out.xisf");
    }
    const working_directory moved(directory.path() / "finished");
    sink->write(bytes("content"));
    sink->finish();
    EXPECT_EQ(entries(directory.path() / "made"), std::vector<std::string>{"out.xisf"});
    EXPECT_EQ(read_file(directory.path() / "made" / "out.xisf"), "content");
    EXPECT_TRUE(entries(directory.path() / "finished").empty());
}

TEST(file_io, relative_paths_longer_than_260_characters)
{
    // From the working directory, which the test changes while it runs.
    const temp_directory directory;
    const std::filesystem::path folder = directory.path() / std::string(100, 'd') / std::string(100, 'e');
    std::filesystem::create_directory(openxisf::test::long_form(folder.parent_path()));
    std::filesystem::create_directory(openxisf::test::long_form(folder));
    const working_directory moved(directory.path());
    const std::string path =
        std::string(100, 'd') + "/" + std::string(100, 'e') + "/" + std::string(100, 'f') + ".xisf";

    write_through_sink(path, "content");
    const file_source source(path);

    EXPECT_EQ(text(read_all(source)), "content");
}

#if defined(_WIN32)
TEST(file_io, network_paths)
{
    // The temporary directory through the administrative share of its drive, \\localhost\C$\..., and the same path in
    // the form that the system takes beyond 260 characters, \\?\UNC\localhost\C$\....
    const temp_directory directory;
    const std::string local = utf8(directory.path());
    if (local.size() < 3 || local[1] != ':' || local[2] != '\\') {
        GTEST_SKIP() << "the temporary directory is not on a drive";
    }
    // localhost\C$\Users\...
    const std::string share = R"(localhost\)" + std::string(1, local[0]) + "$" + local.substr(2);
    std::error_code error;
    if (!std::filesystem::exists(openxisf::test::path_of(R"(\\)" + share), error)) {
        GTEST_SKIP() << "the administrative share of the drive is not available";
    }

    write_through_sink(R"(\\)" + share + R"(\unc.xisf)", "through the share");
    EXPECT_EQ(read_file(directory.path() / "unc.xisf"), "through the share");
    const file_source source(R"(\\?\UNC\)" + share + R"(\unc.xisf)");
    EXPECT_EQ(text(read_all(source)), "through the share");
}
#endif

TEST(file_sink, creates_the_target_only_on_finish)
{
    const temp_directory directory;
    const std::string target = directory.file("unit.xisf");
    file_sink sink(target);
    sink.write(bytes("0123456789"));

    EXPECT_FALSE(std::filesystem::exists(directory.path() / "unit.xisf"));
    ASSERT_EQ(entries(directory.path()).size(), 1U);
    EXPECT_TRUE(entries(directory.path())[0].starts_with(".openxisf-"));

    sink.finish();

    EXPECT_EQ(entries(directory.path()), std::vector<std::string>{"unit.xisf"});
    EXPECT_EQ(read_file(directory.path() / "unit.xisf"), "0123456789");
}

TEST(file_sink, rewrites_bytes_already_written)
{
    const temp_directory directory;
    file_sink sink(directory.file("unit.xisf"));
    EXPECT_TRUE(sink.can_rewrite());

    sink.write(bytes("0123456789"));
    sink.rewrite(2, bytes("ab"));
    sink.write(bytes("!"));
    EXPECT_EQ(sink.position(), 11U);
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [&] { sink.rewrite(10, bytes("xy")); }));
    sink.finish();

    EXPECT_EQ(read_file(directory.path() / "unit.xisf"), "01ab456789!");
}

TEST(file_sink, a_sink_destroyed_before_finish_leaves_the_target_as_it_was)
{
    const temp_directory directory;
    write_file(directory.path() / "old.xisf", bytes("old content"));
    {
        file_sink replacing(directory.file("old.xisf"));
        file_sink creating(directory.file("new.xisf"));
        replacing.write(bytes("new content"));
        creating.write(bytes("new content"));
    }

    EXPECT_EQ(entries(directory.path()), std::vector<std::string>{"old.xisf"});
    EXPECT_EQ(read_file(directory.path() / "old.xisf"), "old content");
}

// The pattern of a writer that fails part of the way through a unit.
TEST(file_sink, a_write_that_fails_part_of_the_way_leaves_the_target_as_it_was)
{
    const temp_directory directory;
    write_file(directory.path() / "unit.xisf", bytes("old content"));
    {
        file_sink sink(directory.file("unit.xisf"));
        openxisf::test::faulty_sink faulty(sink, 3, openxisf::test::fault::short_transfer);
        EXPECT_THROW(
            {
                for (int block = 0; block < 5; ++block) {
                    faulty.write(pattern(1000));
                }
                faulty.finish();
            },
            io_error);
    }

    EXPECT_EQ(entries(directory.path()), std::vector<std::string>{"unit.xisf"});
    EXPECT_EQ(read_file(directory.path() / "unit.xisf"), "old content");
}

TEST(file_sink, a_finish_that_fails_removes_the_temporary_file)
{
    const temp_directory directory;
    std::filesystem::create_directory(directory.path() / "target");
    write_file(directory.path() / "target" / "inside.txt", bytes("kept"));
    file_sink sink(directory.file("target"));
    sink.write(bytes("content"));

    EXPECT_TRUE(throws<io_error>(errc::write_failed, [&] { sink.finish(); }));
    EXPECT_EQ(entries(directory.path()), std::vector<std::string>{"target"});
    EXPECT_EQ(read_file(directory.path() / "target" / "inside.txt"), "kept");
}

TEST(file_sink, nothing_can_be_written_after_finish)
{
    const temp_directory directory;
    file_sink sink(directory.file("unit.xisf"));
    sink.write(bytes("abc"));
    sink.finish();

    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [&] { sink.write(bytes("d")); }));
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [&] { sink.rewrite(0, bytes("d")); }));
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [&] { sink.finish(); }));
    EXPECT_EQ(read_file(directory.path() / "unit.xisf"), "abc");
}

TEST(file_sink, flushes_to_disk_when_asked)
{
    const temp_directory directory;
    file_sink sink(directory.file("unit.xisf"), {.flush_to_disk = true});
    sink.write(bytes("durable"));
    sink.finish();

    EXPECT_EQ(read_file(directory.path() / "unit.xisf"), "durable");
}

// A unit read, changed and saved under the same path.
TEST(file_sink, replaces_a_file_that_a_source_has_open)
{
    const temp_directory directory;
    const std::string path = directory.file("unit.xisf");
    write_file(directory.path() / "unit.xisf", bytes("old content"));
    const file_source source(path);

    write_through_sink(path, "new content");

    EXPECT_EQ(text(read_all(source)), "old content");
    EXPECT_EQ(read_file(directory.path() / "unit.xisf"), "new content");
}

TEST(file_sink, replaces_a_symbolic_link_itself)
{
    const temp_directory directory;
    write_file(directory.path() / "real.xisf", bytes("old content"));
    if (!openxisf::test::create_symbolic_link(directory.path() / "real.xisf", directory.path() / "link.xisf")) {
        GTEST_SKIP() << "the system does not let this process create symbolic links";
    }

    write_through_sink(directory.file("link.xisf"), "new content");

    EXPECT_FALSE(std::filesystem::is_symlink(directory.path() / "link.xisf"));
    EXPECT_EQ(read_file(directory.path() / "link.xisf"), "new content");
    EXPECT_EQ(read_file(directory.path() / "real.xisf"), "old content");
}

TEST(file_sink, replaces_a_read_only_target_except_on_windows)
{
    const temp_directory directory;
    const std::filesystem::path target = directory.path() / "unit.xisf";
    write_file(target, bytes("old content"));
    constexpr std::filesystem::perms writable = std::filesystem::perms::owner_write |
                                                std::filesystem::perms::group_write |
                                                std::filesystem::perms::others_write;
    std::filesystem::permissions(target, writable, std::filesystem::perm_options::remove);

#if defined(_WIN32)
    // The read-only attribute keeps a file from being replaced, as it keeps it from being deleted.
    file_sink sink(directory.file("unit.xisf"));
    sink.write(bytes("new content"));
    EXPECT_TRUE(throws<io_error>(errc::write_failed, [&] { sink.finish(); }));
    EXPECT_EQ(read_file(target), "old content");
    std::filesystem::permissions(target, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
#else
    // Replacing a file takes write access to its directory, not to the file.
    write_through_sink(directory.file("unit.xisf"), "new content");
    EXPECT_EQ(read_file(target), "new content");
#endif
    EXPECT_EQ(entries(directory.path()), std::vector<std::string>{"unit.xisf"});
}

TEST(file_sink, two_sinks_of_one_target_replace_it_in_turn)
{
    const temp_directory directory;
    file_sink first(directory.file("unit.xisf"));
    file_sink second(directory.file("unit.xisf"));
    first.write(bytes("first"));
    second.write(bytes("second"));

    first.finish();
    EXPECT_EQ(read_file(directory.path() / "unit.xisf"), "first");
    second.finish();
    EXPECT_EQ(read_file(directory.path() / "unit.xisf"), "second");
    EXPECT_EQ(entries(directory.path()), std::vector<std::string>{"unit.xisf"});
}

TEST(file_sink, a_missing_directory_is_open_failed)
{
    const temp_directory directory;

    EXPECT_TRUE(throws<io_error>(errc::open_failed, [&] { file_sink sink(directory.file("missing/unit.xisf")); }));
}

TEST(file_sink, refuses_empty_and_invalid_utf8_paths)
{
    EXPECT_TRUE(throws<usage_error>(errc::invalid_argument, [] { file_sink sink(""); }));
    EXPECT_TRUE(throws<usage_error>(errc::invalid_utf8, [] { file_sink sink("a\xC3.xisf"); }));
}

} // namespace
