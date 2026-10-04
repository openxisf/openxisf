// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <openxisf/error.h>
#include <openxisf/io.h>

#include "io/range_check.h"

#include <utility>

namespace openxisf {

callback_source::callback_source(std::uint64_t size, read_function read, callback_source_options options)
    : size_(size), read_(std::move(read)), options_(std::move(options))
{
    if (!read_) {
        throw usage_error(errc::invalid_argument, "a callback source needs a read function");
    }
}

std::uint64_t callback_source::size() const
{
    return size_;
}

void callback_source::read(std::uint64_t offset, std::span<std::byte> destination) const
{
    detail::check_read_range(size_, offset, destination.size(), options_.description);
    read_(offset, destination);
}

bool callback_source::supports_concurrent_reads() const
{
    return options_.concurrent_reads;
}

std::string callback_source::description() const
{
    return options_.description;
}

callback_sink::callback_sink(write_function write, rewrite_function rewrite, finish_function finish)
    : write_(std::move(write)), rewrite_(std::move(rewrite)), finish_(std::move(finish))
{
    if (!write_) {
        throw usage_error(errc::invalid_argument, "a callback sink needs a write function");
    }
}

void callback_sink::write(std::span<const std::byte> data)
{
    write_(data);
    position_ += data.size();
}

std::uint64_t callback_sink::position() const
{
    return position_;
}

bool callback_sink::can_rewrite() const
{
    return static_cast<bool>(rewrite_);
}

void callback_sink::rewrite(std::uint64_t offset, std::span<const std::byte> data)
{
    if (!rewrite_) {
        output_sink::rewrite(offset, data);
        return;
    }
    detail::check_rewrite_range(position_, offset, data.size());
    rewrite_(offset, data);
}

void callback_sink::finish()
{
    if (finish_) {
        finish_();
    }
}

} // namespace openxisf
