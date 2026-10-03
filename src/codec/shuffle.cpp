// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

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
    const std::size_t items = item_size == 0 ? 0 : input.size() / item_size;
    if (item_size <= 1 || items == 0) {
        std::ranges::copy(input, output.begin());
        return;
    }
    std::byte* next = output.data();
    for (std::size_t j = 0; j < item_size; ++j) {
        for (std::size_t i = 0; i < items; ++i) {
            *next++ = input[(i * item_size) + j];
        }
    }
    std::ranges::copy(input.subspan(items * item_size), next);
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
