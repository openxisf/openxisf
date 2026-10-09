// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The paths of external files: UNIX syntax in a unit (spec §10.3), the syntax of the system for the system.

#include "io/paths.h"

#include <openxisf/error.h>

#include "io/native_file.h"
#include "support/temp_directory.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace {

using openxisf::errc;
using openxisf::detail::absolute_system_path;
using openxisf::detail::is_inside;
using openxisf::detail::native_file;
using openxisf::detail::parent_directory;
using openxisf::detail::relative_system_path;
using openxisf::test::throws;

TEST(paths, the_parent_directory_of_a_file)
{
    EXPECT_EQ(parent_directory("unit.xish"), ".");
    EXPECT_EQ(parent_directory("dir/unit.xish"), "dir");
    EXPECT_EQ(parent_directory("a/b/unit.xish"), "a/b");
    EXPECT_EQ(parent_directory("/unit.xish"), "/");
    EXPECT_EQ(parent_directory("/data/unit.xish"), "/data");
#if defined(_WIN32)
    EXPECT_EQ(parent_directory(R"(dir\unit.xish)"), "dir");
    EXPECT_EQ(parent_directory(R"(C:\unit.xish)"), R"(C:\)");
    EXPECT_EQ(parent_directory("C:/unit.xish"), "C:/");
    EXPECT_EQ(parent_directory(R"(C:\data\unit.xish)"), R"(C:\data)");
    EXPECT_EQ(parent_directory("C:unit.xish"), "C:.");
    EXPECT_EQ(parent_directory(R"(\unit.xish)"), R"(\)");
    EXPECT_EQ(parent_directory(R"(\\server\share\unit.xish)"), R"(\\server\share)");
    EXPECT_EQ(parent_directory(R"(\\?\C:\unit.xish)"), R"(\\?\C:\)");
#else
    // A backslash is a character of a file name.
    EXPECT_EQ(parent_directory(R"(dir\unit.xish)"), ".");
#endif
}

TEST(paths, a_relative_path_from_a_directory)
{
    EXPECT_EQ(relative_system_path("dir", "unit.xisb"), "dir/unit.xisb");
    EXPECT_EQ(relative_system_path("/", "unit.xisb"), "/unit.xisb");
    EXPECT_EQ(relative_system_path("dir/", "a/b(1).dat"), "dir/a/b(1).dat");
    EXPECT_EQ(relative_system_path("", "unit.xisb"), "./unit.xisb");
#if defined(_WIN32)
    EXPECT_EQ(relative_system_path(R"(C:\)", "unit.xisb"), R"(C:\unit.xisb)");
    EXPECT_EQ(relative_system_path("C:.", "unit.xisb"), "C:./unit.xisb");
    // UNIX syntax allows what file names on Windows cannot hold, and what Windows takes for a separator or a drive.
    for (const std::string_view path : {R"(..\..\secret.xisb)", "C:/secret.xisb", "file.xisb:stream", "a*.xisb",
                                        "a?.xisb", "a\".xisb", "a<b.xisb", "a>b.xisb", "a|b.xisb", "a\tb.xisb"}) {
        EXPECT_TRUE(throws<openxisf::unsupported_error>(errc::unsupported_location, [path] {
            (void)relative_system_path("dir", path);
        })) << path;
    }
#else
    EXPECT_EQ(relative_system_path("dir", R"(a\b:c.xisb)"), R"(dir/a\b:c.xisb)");
#endif
}

TEST(paths, an_absolute_path)
{
#if defined(_WIN32)
    EXPECT_EQ(absolute_system_path("/c/data/unit.xisb"), "C:/data/unit.xisb");
    EXPECT_EQ(absolute_system_path("/D/unit.xisb"), "D:/unit.xisb");
    EXPECT_EQ(absolute_system_path("/c"), "C:/");
    EXPECT_TRUE(throws<openxisf::unsupported_error>(errc::location_not_allowed,
                                                    [] { (void)absolute_system_path("//server/share/unit.xisb"); }));
    for (const std::string_view path : {"/data/unit.xisb", "/", "/1/unit.xisb", "/cd/unit.xisb", "/cd", "/c:/unit.xisb",
                                        "/c/dir\\unit.xisb", "/c/unit.xisb:stream"}) {
        EXPECT_TRUE(throws<openxisf::unsupported_error>(errc::unsupported_location, [path] {
            (void)absolute_system_path(path);
        })) << path;
    }
#else
    EXPECT_EQ(absolute_system_path("/data/unit.xisb"), "/data/unit.xisb");
    EXPECT_EQ(absolute_system_path("//data/unit.xisb"), "//data/unit.xisb");
#endif
}

TEST(paths, a_path_inside_a_directory)
{
    EXPECT_TRUE(is_inside("/data/unit.xisb", "/data"));
    EXPECT_TRUE(is_inside("/data/a/unit.xisb", "/data"));
    EXPECT_TRUE(is_inside("/unit.xisb", "/"));
    EXPECT_FALSE(is_inside("/data", "/data"));
    EXPECT_FALSE(is_inside("/database/unit.xisb", "/data"));
    EXPECT_FALSE(is_inside("/other/unit.xisb", "/data"));
    EXPECT_FALSE(is_inside("/data/unit.xisb", ""));
#if defined(_WIN32)
    EXPECT_TRUE(is_inside(R"(\\?\C:\data\unit.xisb)", R"(\\?\C:\data)"));
    EXPECT_TRUE(is_inside(R"(\\?\C:\unit.xisb)", R"(\\?\C:\)"));
    EXPECT_FALSE(is_inside(R"(\\?\C:\database\unit.xisb)", R"(\\?\C:\data)"));
    EXPECT_FALSE(is_inside(R"(\\?\D:\data\unit.xisb)", R"(\\?\C:\data)"));
#endif
}

TEST(paths, a_canonical_path_names_the_file_itself)
{
    const openxisf::test::temp_directory directory;
    std::filesystem::create_directory(directory.path() / "sub");
    const std::string base = native_file::find(openxisf::test::utf8(directory.path())).name();
    EXPECT_EQ(native_file::find(openxisf::test::utf8(directory.path() / "sub" / "..")).name(), base);
    EXPECT_TRUE(is_inside(native_file::find(openxisf::test::utf8(directory.path() / "sub")).name(), base));
    EXPECT_TRUE(throws<openxisf::io_error>(errc::open_failed, [&directory] {
        (void)native_file::find(openxisf::test::utf8(directory.path() / "missing"));
    }));
#if defined(_WIN32)
    // A device has no path in a directory.
    EXPECT_TRUE(throws<openxisf::io_error>(errc::not_a_regular_file, [] { (void)native_file::find(R"(\\.\NUL)"); }));
#endif
}

} // namespace
