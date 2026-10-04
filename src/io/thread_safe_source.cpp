// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "io/thread_safe_source.h"

#include <openxisf/error.h>

#include "io/range_check.h"

#include <utility>

namespace openxisf::detail {

namespace {

std::unique_ptr<input_source> require(std::unique_ptr<input_source> source)
{
    if (!source) {
        throw usage_error(errc::invalid_argument, "the source is null");
    }
    return source;
}

} // namespace

thread_safe_source::thread_safe_source(std::unique_ptr<input_source> source)
    : source_(require(std::move(source))), size_(source_->size()), description_(source_->description()),
      concurrent_(source_->supports_concurrent_reads())
{}

void thread_safe_source::read(std::uint64_t offset, std::span<std::byte> destination) const
{
    check_read_range(size_, offset, destination.size(), description_);
    if (destination.empty()) {
        return;
    }
    if (concurrent_) {
        source_->read(offset, destination);
    } else {
        const std::scoped_lock lock(mutex_);
        source_->read(offset, destination);
    }
}

} // namespace openxisf::detail
