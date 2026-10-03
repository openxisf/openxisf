// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "core/endian.h"

#include <algorithm>
#include <cstdint>

namespace openxisf::detail {

namespace {

template <std::unsigned_integral T> void swap_items(std::span<std::byte> data) noexcept
{
    const std::size_t count = data.size() / sizeof(T);
    for (std::size_t i = 0; i < count; ++i) {
        std::byte* item = data.data() + i * sizeof(T);
        T value = 0;
        std::memcpy(&value, item, sizeof(T));
        value = byte_swap(value);
        std::memcpy(item, &value, sizeof(T));
    }
}

} // namespace

void swap_byte_order(std::span<std::byte> data, std::size_t item_size) noexcept
{
    switch (item_size) {
    case 2:
        swap_items<std::uint16_t>(data);
        break;
    case 4:
        swap_items<std::uint32_t>(data);
        break;
    case 8:
        swap_items<std::uint64_t>(data);
        break;
    default:
        // One-byte items have no byte order. Other sizes, such as the 16 bytes of 128-bit scalars, are rare.
        if (item_size > 1) {
            for (std::size_t start = 0; data.size() - start >= item_size; start += item_size) {
                const auto item = data.subspan(start, item_size);
                std::ranges::reverse(item);
            }
        }
        break;
    }
}

} // namespace openxisf::detail
