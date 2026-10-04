// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// file_resolver(): path() locations to files, confined to the directory of the header file (spec §10.3), and the
// traversals that it refuses.

#include <openxisf/error.h>
#include <openxisf/io.h>

#include "support/bytes.h"
#include "support/files.h"
#include "support/links.h"
#include "support/temp_directory.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using openxisf::errc;
using openxisf::external_reference;
using openxisf::location_form;
using openxisf::test::bytes;
using openxisf::test::path_of;
using openxisf::test::temp_directory;
using openxisf::test::throws;
using openxisf::test::utf8;
using openxisf::test::write_file;

// A header directory, unit, with a file and a subdirectory, next to a file that is outside it.
class file_resolver_test : public testing::Test
{
protected:
    file_resolver_test()
    {
        std::filesystem::create_directories(unit_directory / "sub");
        write_file(unit_directory / "data.xisb", bytes("inside"));
        write_file(unit_directory / "sub" / "more.dat", bytes("deeper"));
        write_file(root.path() / "outside.dat", bytes("outside"));
    }

    [[nodiscard]] std::string directory() const
    {
        return utf8(unit_directory);
    }

    // The content of the file that the resolver opens for a relative path.
    [[nodiscard]] static std::string open(std::string_view relative, const openxisf::external_resolver& resolver)
    {
        const std::unique_ptr<openxisf::input_source> source =
            resolver(external_reference{.form = location_form::relative_path, .location = std::string(relative)});
        if (!source) {
            ADD_FAILURE() << "no source for " << relative;
            return {};
        }
        std::vector<std::byte> data(source->size());
        source->read(0, data);
        return openxisf::test::text(data);
    }

    [[nodiscard]] std::string open(std::string_view relative) const
    {
        return open(relative, openxisf::file_resolver(directory()));
    }

    template <typename Error> [[nodiscard]] testing::AssertionResult refuses(std::string_view relative, errc code) const
    {
        return throws<Error>(code, [this, relative] { (void)open(relative); });
    }

    temp_directory root;
    std::filesystem::path unit_directory = root.path() / "unit";
};

TEST_F(file_resolver_test, opens_a_file_inside_the_directory)
{
    EXPECT_EQ(open("data.xisb"), "inside");
    EXPECT_EQ(open("sub/more.dat"), "deeper");
    EXPECT_EQ(open("./data.xisb"), "inside");
    EXPECT_EQ(open("sub//../data.xisb"), "inside");
}

TEST_F(file_resolver_test, opens_a_directory_of_any_name)
{
    const std::filesystem::path directory = root.path() / path_of("Nebulosa del Ñandú (星雲)");
    std::filesystem::create_directory(directory);
    write_file(directory / path_of("ñandú.xisb"), bytes("named"));
    EXPECT_EQ(open("ñandú.xisb", openxisf::file_resolver(utf8(directory))), "named");
}

TEST_F(file_resolver_test, refuses_a_path_that_leads_outside_the_directory)
{
    EXPECT_TRUE(refuses<openxisf::unsupported_error>("../outside.dat", errc::location_not_allowed));
    EXPECT_TRUE(refuses<openxisf::unsupported_error>("sub/../../outside.dat", errc::location_not_allowed));
    EXPECT_TRUE(refuses<openxisf::unsupported_error>("../unit/../outside.dat", errc::location_not_allowed));
    // The directory itself is not inside it.
    EXPECT_TRUE(refuses<openxisf::unsupported_error>(".", errc::location_not_allowed));
    EXPECT_TRUE(refuses<openxisf::unsupported_error>("../unit", errc::location_not_allowed));
}

TEST_F(file_resolver_test, refuses_a_file_that_does_not_exist)
{
    EXPECT_TRUE(refuses<openxisf::io_error>("missing.xisb", errc::open_failed));
    EXPECT_TRUE(refuses<openxisf::io_error>("../missing.xisb", errc::open_failed));
    EXPECT_TRUE(throws<openxisf::io_error>(errc::open_failed, [this] {
        (void)open("data.xisb", openxisf::file_resolver(utf8(root.path() / "missing")));
    }));
}

TEST_F(file_resolver_test, refuses_what_is_not_a_regular_file)
{
    EXPECT_TRUE(refuses<openxisf::io_error>("sub", errc::not_a_regular_file));
#if defined(_WIN32)
    // Device names, which Windows reserves in every directory.
    for (const std::string_view device : {"NUL", "CON", "COM1", "aux", "nul.txt", "LPT1.xisb"}) {
        try {
            (void)open(device);
            ADD_FAILURE() << device << " was opened";
        } catch (const openxisf::io_error& failure) {
            EXPECT_TRUE(failure.code() == errc::not_a_regular_file || failure.code() == errc::open_failed) << device;
        }
    }
#else
    ASSERT_EQ(::mkfifo((unit_directory / "fifo").c_str(), 0600), 0);
    EXPECT_TRUE(refuses<openxisf::io_error>("fifo", errc::not_a_regular_file));
#endif
}

TEST_F(file_resolver_test, follows_symbolic_links_only_inside_the_directory)
{
    if (!openxisf::test::create_symbolic_link(unit_directory / "data.xisb", unit_directory / "inside.xisb")) {
        GTEST_SKIP() << "the system does not let this process create symbolic links";
    }
    EXPECT_EQ(open("inside.xisb"), "inside");
    ASSERT_TRUE(openxisf::test::create_symbolic_link(root.path() / "outside.dat", unit_directory / "escape.xisb"));
    EXPECT_TRUE(refuses<openxisf::unsupported_error>("escape.xisb", errc::location_not_allowed));
    ASSERT_TRUE(openxisf::test::create_symbolic_link(root.path(), unit_directory / "up"));
    EXPECT_TRUE(refuses<openxisf::unsupported_error>("up/outside.dat", errc::location_not_allowed));
    // A link that leaves and comes back stays inside.
    EXPECT_EQ(open("up/unit/data.xisb"), "inside");
}

#if defined(_WIN32)
TEST_F(file_resolver_test, follows_junctions_only_inside_the_directory)
{
    openxisf::test::create_junction(root.path(), unit_directory / "up");
    EXPECT_TRUE(refuses<openxisf::unsupported_error>("up/outside.dat", errc::location_not_allowed));
    EXPECT_EQ(open("up/unit/data.xisb"), "inside");
    openxisf::test::create_junction(unit_directory / "sub", unit_directory / "down");
    EXPECT_EQ(open("down/more.dat"), "deeper");
}

TEST_F(file_resolver_test, refuses_what_names_no_file_on_windows)
{
    EXPECT_TRUE(refuses<openxisf::unsupported_error>(R"(sub\more.dat)", errc::unsupported_location));
    EXPECT_TRUE(refuses<openxisf::unsupported_error>(R"(..\outside.dat)", errc::unsupported_location));
    EXPECT_TRUE(refuses<openxisf::unsupported_error>("data.xisb:stream", errc::unsupported_location));
    EXPECT_TRUE(refuses<openxisf::unsupported_error>("C:/outside.dat", errc::unsupported_location));
}
#endif

TEST_F(file_resolver_test, refuses_absolute_paths_unless_allowed)
{
    // The absolute path of outside.dat in UNIX syntax: /c/... on Windows.
    std::string path = utf8(std::filesystem::absolute(root.path() / "outside.dat"));
#if defined(_WIN32)
    for (char& c : path) {
        c = c == '\\' ? '/' : c;
    }
    ASSERT_EQ(path[1], ':');
    path = "/" + std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(path[0])))) + path.substr(2);
#endif
    const external_reference reference{.form = location_form::absolute_path, .location = path};
    EXPECT_TRUE(throws<openxisf::unsupported_error>(errc::location_not_allowed,
                                                    [&] { (void)openxisf::file_resolver(directory())(reference); }));
    const std::unique_ptr<openxisf::input_source> source =
        openxisf::file_resolver(directory(), {.absolute_paths = true})(reference);
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(source->size(), 7U);
}

TEST_F(file_resolver_test, refuses_urls)
{
    const external_reference reference{.form = location_form::url, .location = "file:///data/unit.xisb"};
    EXPECT_TRUE(throws<openxisf::unsupported_error>(errc::unsupported_location, [&] {
        (void)openxisf::file_resolver(directory(), {.absolute_paths = true})(reference);
    }));
}

TEST(file_resolver, needs_a_valid_directory)
{
    EXPECT_TRUE(throws<openxisf::usage_error>(errc::invalid_argument, [] { (void)openxisf::file_resolver(""); }));
    EXPECT_TRUE(throws<openxisf::usage_error>(errc::invalid_utf8, [] { (void)openxisf::file_resolver("a\xFF"); }));
}

} // namespace
