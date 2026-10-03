// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

// Reads from 16 threads at once, on sources that support concurrent reads and on sources that the library serializes.
// ThreadSanitizer runs these tests in CI.

#include <openxisf/io.h>
#include <openxisf/stream_io.h>

#include "core/xoshiro.h"
#include "io/thread_safe_source.h"
#include "support/bytes.h"
#include "support/files.h"
#include "support/temp_directory.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using openxisf::detail::thread_safe_source;
using openxisf::test::pattern;
using openxisf::test::temp_directory;

constexpr int thread_count = 16;
constexpr int reads_per_thread = 200;
constexpr std::size_t max_read = std::size_t{64} << 10;

// Every thread reads ranges of its own and compares them with the expected bytes. Returns the number of wrong reads,
// failed reads included.
template <typename Source> int read_from_threads(const Source& source, std::span<const std::byte> expected)
{
    std::atomic<int> wrong{0};
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([&source, expected, &wrong, t] {
            openxisf::detail::xoshiro256starstar random({1, 2, 3, static_cast<std::uint64_t>(t) + 1});
            std::vector<std::byte> destination;
            for (int i = 0; i < reads_per_thread; ++i) {
                const std::size_t offset = random() % expected.size();
                const std::size_t length = random() % std::min(max_read, expected.size() - offset);
                destination.resize(length);
                try {
                    source.read(offset, destination);
                    if (!std::ranges::equal(destination, expected.subspan(offset, length))) {
                        ++wrong;
                    }
                } catch (const std::exception&) {
                    ++wrong;
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    return wrong;
}

// A source written the simple way, with a cursor: it reads correctly only when its calls do not overlap, which it
// records. ThreadSanitizer would also report the race on the cursor.
class cursor_source final : public openxisf::input_source
{
public:
    explicit cursor_source(std::span<const std::byte> data) : data_(data) {}

    std::uint64_t size() const override
    {
        return data_.size();
    }

    void read(std::uint64_t offset, std::span<std::byte> destination) const override
    {
        if (++active_ != 1) {
            overlapped_ = true;
        }
        cursor_ = offset;
        std::this_thread::yield();
        // Another read may have moved the cursor meanwhile. The copy stays within the data, and the wrong bytes show.
        const std::uint64_t start = std::min<std::uint64_t>(cursor_, data_.size() - destination.size());
        std::ranges::copy(data_.subspan(start, destination.size()), destination.begin());
        --active_;
    }

    [[nodiscard]] bool overlapped() const noexcept
    {
        return overlapped_;
    }

private:
    std::span<const std::byte> data_;
    mutable std::uint64_t cursor_ = 0;
    mutable std::atomic<int> active_{0};
    mutable std::atomic<bool> overlapped_{false};
};

TEST(concurrency_io, file_source)
{
    const temp_directory directory;
    const std::vector<std::byte> data = pattern(std::size_t{1} << 20);
    openxisf::test::write_file(directory.path() / "data.bin", data);
    const std::string path = directory.file("data.bin");

    const openxisf::file_source direct(path);
    const thread_safe_source wrapped(std::make_unique<openxisf::file_source>(path));

    EXPECT_EQ(read_from_threads(direct, data), 0);
    EXPECT_EQ(read_from_threads(wrapped, data), 0);
}

TEST(concurrency_io, memory_source)
{
    const std::vector<std::byte> data = pattern(std::size_t{1} << 20);
    const openxisf::memory_source source{std::span<const std::byte>(data)};

    EXPECT_EQ(read_from_threads(source, data), 0);
}

TEST(concurrency_io, a_source_without_concurrent_reads_is_called_one_read_at_a_time)
{
    const std::vector<std::byte> data = pattern(std::size_t{1} << 20);
    auto inner = std::make_unique<cursor_source>(data);
    const cursor_source& cursor = *inner;
    const thread_safe_source source(std::move(inner));

    EXPECT_EQ(read_from_threads(source, data), 0);
    EXPECT_FALSE(cursor.overlapped());
}

TEST(concurrency_io, stream_sources)
{
    const temp_directory directory;
    const std::vector<std::byte> data = pattern(std::size_t{1} << 20);
    openxisf::test::write_file(directory.path() / "data.bin", data);
    const openxisf::test::c_file file(directory.path() / "data.bin", "rb");
    std::istringstream stream(openxisf::test::text(data));

    const thread_safe_source from_file(std::make_unique<openxisf::stdio_source>(file.get()));
    const thread_safe_source from_stream(std::make_unique<openxisf::istream_source>(stream));

    EXPECT_EQ(read_from_threads(from_file, data), 0);
    EXPECT_EQ(read_from_threads(from_stream, data), 0);
}

} // namespace
