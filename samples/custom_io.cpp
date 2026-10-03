// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

// Custom I/O. Units are read from an openxisf::input_source and written to an openxisf::output_sink, which an
// application can implement over its own storage. This program implements a source over the function table of a
// storage library written in C, and a sink that writes into a container of the application. It writes through the
// sink the way a streaming writer does, then reads the result back through the source.

#include <openxisf/openxisf.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace {

// The interface of a storage library written in C: an opaque blob and a table of functions. read_at returns 0 on
// success.
struct blob_functions
{
    std::uint64_t (*size)(const void* blob);
    int (*read_at)(const void* blob, std::uint64_t offset, void* buffer, std::size_t length);
};

// A source over the function table. It does not declare concurrent reads, so the library calls read() from one thread
// at a time and the C functions need no locking.
class blob_source final : public openxisf::input_source
{
public:
    blob_source(const blob_functions& functions, const void* blob) : functions_(functions), blob_(blob) {}

    std::uint64_t size() const override
    {
        return functions_.size(blob_);
    }

    void read(std::uint64_t offset, std::span<std::byte> destination) const override
    {
        if (functions_.read_at(blob_, offset, destination.data(), destination.size()) != 0) {
            throw openxisf::io_error(openxisf::errc::read_failed, "cannot read the blob", {}, {.offset = offset});
        }
    }

    std::string description() const override
    {
        return "the blob";
    }

private:
    blob_functions functions_;
    const void* blob_;
};

// A sink into a std::deque that the application owns. It supports rewrite(), which lets a writer reserve room for a
// header and fill it in at the end.
class deque_sink final : public openxisf::output_sink
{
public:
    explicit deque_sink(std::deque<std::byte>& bytes) : bytes_(bytes) {}

    void write(std::span<const std::byte> data) override
    {
        bytes_.insert(bytes_.end(), data.begin(), data.end());
    }

    std::uint64_t position() const override
    {
        return bytes_.size();
    }

    bool can_rewrite() const override
    {
        return true;
    }

    void rewrite(std::uint64_t offset, std::span<const std::byte> data) override
    {
        std::ranges::copy(data, bytes_.begin() + static_cast<std::ptrdiff_t>(offset));
    }

private:
    std::deque<std::byte>& bytes_;
};

// The storage library, played here by a std::deque.
std::uint64_t deque_size(const void* blob)
{
    return static_cast<const std::deque<std::byte>*>(blob)->size();
}

int deque_read_at(const void* blob, std::uint64_t offset, void* buffer, std::size_t length)
{
    const auto& bytes = *static_cast<const std::deque<std::byte>*>(blob);
    if (offset > bytes.size() || length > bytes.size() - offset) {
        return -1;
    }
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), length, static_cast<std::byte*>(buffer));
    return 0;
}

} // namespace

int main()
{
    try {
        std::deque<std::byte> storage;
        deque_sink sink(storage);

        // Room for a 4-byte header, then the data, then the header in its room: its value, the length of the data, is
        // known only at the end.
        const std::string_view text = "written through a custom sink, read through a custom source";
        sink.write(std::array<std::byte, 4>{});
        sink.write(std::as_bytes(std::span(text)));
        const std::uint64_t length = sink.position() - 4;
        std::array<std::byte, 4> header{};
        for (std::size_t i = 0; i < header.size(); ++i) {
            header[i] = static_cast<std::byte>((length >> (8 * i)) & 0xFFU);
        }
        sink.rewrite(0, header);
        sink.finish();

        const blob_functions functions{.size = deque_size, .read_at = deque_read_at};
        const blob_source source(functions, &storage);
        source.read(0, header);
        std::size_t stored_length = 0;
        for (std::size_t i = 0; i < header.size(); ++i) {
            stored_length |= std::to_integer<std::size_t>(header[i]) << (8 * i);
        }
        std::string stored(stored_length, '\0');
        source.read(4, std::as_writable_bytes(std::span(stored)));

        std::cout << source.description() << " holds " << source.size() << " bytes: " << stored << '\n';
        return stored == text ? 0 : 1;
    } catch (const std::exception& failure) {
        (void)std::fputs(failure.what(), stderr);
        (void)std::fputs("\n", stderr);
        return 1;
    }
}
