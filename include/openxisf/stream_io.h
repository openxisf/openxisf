// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/error.h>
#include <openxisf/io.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <istream>
#include <ostream>
#include <span>
#include <system_error>

/// @file
/// Sources and sinks over C streams (`std::FILE*`) and C++ streams.
///
/// They are defined inline on purpose: the stream functions then run in the caller's C runtime and standard library,
/// which keeps a stream from crossing runtimes on Windows. None of them supports concurrent reads, so the library
/// calls them from one thread at a time. They never close their stream, which must stay open while they are in use.

namespace openxisf {

/// A C stream from its position at construction to its end. The stream must be open in binary mode and able to seek.
/// Reads move its position.
class stdio_source final : public input_source
{
public:
    /// @throws usage_error when stream is null.
    /// @throws io_error (errc::not_seekable) when the stream cannot seek.
    explicit stdio_source(std::FILE* stream) : stream_(stream)
    {
        if (stream_ == nullptr) {
            throw usage_error(errc::invalid_argument, "the FILE stream is null");
        }
        errno = 0;
        const std::int64_t start = tell(stream_);
        if (start < 0 || !seek(stream_, 0, SEEK_END)) {
            throw io_error(errc::not_seekable, "the FILE stream cannot seek", last_error());
        }
        const std::int64_t end = tell(stream_);
        if (end < start) {
            throw io_error(errc::not_seekable, "the FILE stream cannot seek", last_error());
        }
        start_ = static_cast<std::uint64_t>(start);
        size_ = static_cast<std::uint64_t>(end - start);
    }

    std::uint64_t size() const override
    {
        return size_;
    }

    void read(std::uint64_t offset, std::span<std::byte> destination) const override
    {
        if (offset > size_ || destination.size() > size_ - offset) {
            throw io_error(errc::end_of_data, "the read goes past the end of the FILE stream", {}, {.offset = offset});
        }
        if (destination.empty()) {
            return;
        }
        std::clearerr(stream_);
        errno = 0;
        if (!seek(stream_, static_cast<std::int64_t>(start_ + offset), SEEK_SET)) {
            throw io_error(errc::read_failed, "cannot seek in the FILE stream", last_error(), {.offset = offset});
        }
        if (std::fread(destination.data(), 1, destination.size(), stream_) != destination.size()) {
            // The end of the stream comes early when it is shorter than when the source was made.
            const errc code = std::feof(stream_) != 0 ? errc::end_of_data : errc::read_failed;
            throw io_error(code, "cannot read the FILE stream", last_error(), {.offset = offset});
        }
    }

private:
    static std::int64_t tell(std::FILE* stream) noexcept
    {
#if defined(_WIN32)
        return _ftelli64(stream);
#else
        return ftello(stream);
#endif
    }

    static bool seek(std::FILE* stream, std::int64_t offset, int origin) noexcept
    {
#if defined(_WIN32)
        return _fseeki64(stream, offset, origin) == 0;
#else
        return fseeko(stream, offset, origin) == 0;
#endif
    }

    static std::error_code last_error() noexcept
    {
        const int error = errno;
        return error != 0 ? std::error_code(error, std::generic_category()) : std::error_code();
    }

    std::FILE* stream_;
    std::uint64_t start_ = 0;
    std::uint64_t size_ = 0;
};

/// A C stream written from its position at construction. It supports rewrite() when it can seek, which a pipe cannot.
/// A stream opened in append mode writes every byte at its end, so it must not be given to a sink that rewrites.
class stdio_sink final : public output_sink
{
public:
    /// @throws usage_error when stream is null.
    explicit stdio_sink(std::FILE* stream) : stream_(stream)
    {
        if (stream_ == nullptr) {
            throw usage_error(errc::invalid_argument, "the FILE stream is null");
        }
        const std::int64_t start = tell(stream_);
        rewritable_ = start >= 0;
        start_ = rewritable_ ? static_cast<std::uint64_t>(start) : 0;
    }

    void write(std::span<const std::byte> data) override
    {
        errno = 0;
        if (std::fwrite(data.data(), 1, data.size(), stream_) != data.size()) {
            throw io_error(errc::write_failed, "cannot write to the FILE stream", last_error(), {.offset = position_});
        }
        position_ += data.size();
    }

    std::uint64_t position() const override
    {
        return position_;
    }

    bool can_rewrite() const override
    {
        return rewritable_;
    }

    void rewrite(std::uint64_t offset, std::span<const std::byte> data) override
    {
        if (!rewritable_) {
            throw usage_error(errc::invalid_argument, "the FILE stream cannot seek, so it cannot rewrite");
        }
        if (offset > position_ || data.size() > position_ - offset) {
            throw usage_error(errc::invalid_argument, "a rewrite must stay within the bytes already written");
        }
        errno = 0;
        if (!seek(stream_, static_cast<std::int64_t>(start_ + offset), SEEK_SET) ||
            std::fwrite(data.data(), 1, data.size(), stream_) != data.size() ||
            !seek(stream_, static_cast<std::int64_t>(start_ + position_), SEEK_SET)) {
            throw io_error(errc::write_failed, "cannot rewrite the FILE stream", last_error(), {.offset = offset});
        }
    }

    void finish() override
    {
        errno = 0;
        if (std::fflush(stream_) != 0) {
            throw io_error(errc::write_failed, "cannot flush the FILE stream", last_error());
        }
    }

private:
    static std::int64_t tell(std::FILE* stream) noexcept
    {
#if defined(_WIN32)
        return _ftelli64(stream);
#else
        return ftello(stream);
#endif
    }

    static bool seek(std::FILE* stream, std::int64_t offset, int origin) noexcept
    {
#if defined(_WIN32)
        return _fseeki64(stream, offset, origin) == 0;
#else
        return fseeko(stream, offset, origin) == 0;
#endif
    }

    static std::error_code last_error() noexcept
    {
        const int error = errno;
        return error != 0 ? std::error_code(error, std::generic_category()) : std::error_code();
    }

    std::FILE* stream_;
    std::uint64_t start_ = 0;
    std::uint64_t position_ = 0;
    bool rewritable_ = false;
};

/// A C++ input stream from its position at construction to its end. The stream must be able to seek, and binary on
/// platforms where that matters. The source clears the stream's state and moves its position. When the stream's
/// exceptions() mask is set, the stream's own exceptions pass through unchanged.
class istream_source final : public input_source
{
public:
    /// @throws io_error (errc::not_seekable) when the stream cannot seek.
    explicit istream_source(std::istream& stream) : stream_(stream)
    {
        stream_.clear();
        const std::streamoff start = stream_.tellg();
        if (start >= 0) {
            stream_.seekg(0, std::ios::end);
        }
        const std::streamoff end = stream_.fail() ? -1 : static_cast<std::streamoff>(stream_.tellg());
        if (start < 0 || end < start) {
            throw io_error(errc::not_seekable, "the input stream cannot seek");
        }
        start_ = static_cast<std::uint64_t>(start);
        size_ = static_cast<std::uint64_t>(end - start);
    }

    std::uint64_t size() const override
    {
        return size_;
    }

    void read(std::uint64_t offset, std::span<std::byte> destination) const override
    {
        if (offset > size_ || destination.size() > size_ - offset) {
            throw io_error(errc::end_of_data, "the read goes past the end of the input stream", {}, {.offset = offset});
        }
        if (destination.empty()) {
            return;
        }
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(start_ + offset));
        if (stream_.fail()) {
            throw io_error(errc::read_failed, "cannot seek in the input stream", {}, {.offset = offset});
        }
        const auto count = static_cast<std::streamsize>(destination.size());
        stream_.read(reinterpret_cast<char*>(destination.data()), count);
        if (stream_.gcount() != count) {
            // The end of the stream comes early when it is shorter than when the source was made.
            const errc code = stream_.eof() && !stream_.bad() ? errc::end_of_data : errc::read_failed;
            throw io_error(code, "cannot read the input stream", {}, {.offset = offset});
        }
    }

private:
    std::istream& stream_;
    std::uint64_t start_ = 0;
    std::uint64_t size_ = 0;
};

/// A C++ output stream written from its position at construction. It supports rewrite() when the stream can seek. A
/// stream opened with std::ios::app writes every byte at its end, so it must not be given to a sink that rewrites.
/// When the stream's exceptions() mask is set, the stream's own exceptions pass through unchanged.
class ostream_sink final : public output_sink
{
public:
    explicit ostream_sink(std::ostream& stream) : stream_(stream)
    {
        const std::streamoff start = stream_.tellp();
        rewritable_ = start >= 0;
        start_ = rewritable_ ? static_cast<std::uint64_t>(start) : 0;
    }

    void write(std::span<const std::byte> data) override
    {
        stream_.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (stream_.fail()) {
            throw io_error(errc::write_failed, "cannot write to the output stream", {}, {.offset = position_});
        }
        position_ += data.size();
    }

    std::uint64_t position() const override
    {
        return position_;
    }

    bool can_rewrite() const override
    {
        return rewritable_;
    }

    void rewrite(std::uint64_t offset, std::span<const std::byte> data) override
    {
        if (!rewritable_) {
            throw usage_error(errc::invalid_argument, "the output stream cannot seek, so it cannot rewrite");
        }
        if (offset > position_ || data.size() > position_ - offset) {
            throw usage_error(errc::invalid_argument, "a rewrite must stay within the bytes already written");
        }
        stream_.seekp(static_cast<std::streamoff>(start_ + offset));
        stream_.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        stream_.seekp(static_cast<std::streamoff>(start_ + position_));
        if (stream_.fail()) {
            throw io_error(errc::write_failed, "cannot rewrite the output stream", {}, {.offset = offset});
        }
    }

    void finish() override
    {
        stream_.flush();
        if (stream_.fail()) {
            throw io_error(errc::write_failed, "cannot flush the output stream");
        }
    }

private:
    std::ostream& stream_;
    std::uint64_t start_ = 0;
    std::uint64_t position_ = 0;
    bool rewritable_ = false;
};

} // namespace openxisf
