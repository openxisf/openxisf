// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "codec/shuffle.h"

#include <openxisf/error.h>

#include <algorithm>

namespace openxisf::detail {

namespace {

void check_sizes(std::span<const std::byte> input, std::span<std::byte> output)
{
    if (input.size() != output.size()) {
        throw usage_error(errc::invalid_argument, "byte shuffling needs an output of the size of its input");
    }
}

} // namespace

// Byte j of item i is at i * item_size + j in the unshuffled order, and at j * items + i in the shuffled one.

void shuffle_bytes(std::span<const std::byte> input, std::span<std::byte> output, std::size_t item_size)
{
    check_sizes(input, output);
    shuffle_part(input, item_size, 0, output);
}

void shuffle_part(std::span<const std::byte> input, std::size_t item_size, std::size_t offset,
                  std::span<std::byte> output)
{
    if (offset > input.size() || output.size() > input.size() - offset) {
        throw usage_error(errc::invalid_argument, "a part of a shuffled block must lie within the block");
    }
    const std::size_t items = item_size == 0 ? 0 : input.size() / item_size;
    if (item_size <= 1 || items == 0) {
        std::ranges::copy(input.subspan(offset, output.size()), output.begin());
        return;
    }
    // Run j of the shuffled block holds byte j of every item.
    const std::size_t end = offset + output.size();
    const std::size_t shuffled_end = items * item_size;
    std::size_t position = offset;
    std::byte* next = output.data();
    while (position < end && position < shuffled_end) {
        const std::size_t j = position / items;
        const std::size_t run_end = std::min(end, (j + 1) * items);
        for (std::size_t i = position % items; position < run_end; ++i, ++position) {
            *next++ = input[(i * item_size) + j];
        }
    }
    if (position < end) {
        std::ranges::copy(input.subspan(position, end - position), next);
    }
}

void unshuffle_bytes(std::span<const std::byte> input, std::span<std::byte> output, std::size_t item_size)
{
    check_sizes(input, output);
    const std::size_t items = item_size == 0 ? 0 : input.size() / item_size;
    if (item_size <= 1 || items == 0) {
        std::ranges::copy(input, output.begin());
        return;
    }
    const std::byte* next = input.data();
    for (std::size_t j = 0; j < item_size; ++j) {
        for (std::size_t i = 0; i < items; ++i) {
            output[(i * item_size) + j] = *next++;
        }
    }
    std::ranges::copy(input.subspan(items * item_size),
                      output.begin() + static_cast<std::ptrdiff_t>(items * item_size));
}

} // namespace openxisf::detail
