// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/io.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>

namespace openxisf::test {

/// A source written the simple way, with a cursor, which does not declare concurrent reads: it reads correctly only
/// when its calls do not overlap, which it records. ThreadSanitizer also reports the race on the cursor.
class cursor_source final : public input_source
{
public:
    /// A source of data, which must outlive it.
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

    /// True when two reads ran at once.
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

} // namespace openxisf::test
