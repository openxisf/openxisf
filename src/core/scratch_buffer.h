// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <cstddef>
#include <memory>
#include <span>

namespace openxisf::detail {

/// Memory for bytes that are written completely before they are read, such as the stored bytes of a compressed block.
/// Unlike a std::vector, it is not filled with zeros first, which for a large block costs about as much as the copy
/// that follows.
// The array form of std::unique_ptr is how the standard library allocates bytes without initializing them; a vector
// would need a custom allocator.
// NOLINTBEGIN(modernize-avoid-c-arrays)
class scratch_buffer
{
public:
    explicit scratch_buffer(std::size_t size) : data_(std::make_unique_for_overwrite<std::byte[]>(size)), size_(size) {}

    [[nodiscard]] std::span<std::byte> bytes() noexcept
    {
        return {data_.get(), size_};
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept
    {
        return {data_.get(), size_};
    }

private:
    std::unique_ptr<std::byte[]> data_;
    std::size_t size_;
};
// NOLINTEND(modernize-avoid-c-arrays)

} // namespace openxisf::detail
