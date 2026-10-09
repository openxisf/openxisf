// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "support/virtual_source.h"

#include <algorithm>
#include <span>
#include <utility>

namespace openxisf::test {

std::vector<std::byte> virtual_bytes(std::uint64_t offset, std::size_t size)
{
    std::vector<std::byte> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = virtual_byte(offset + i);
    }
    return bytes;
}

std::unique_ptr<input_source> virtual_source(std::uint64_t size, std::vector<placed_bytes> parts,
                                             std::shared_ptr<std::vector<std::uint64_t>> reads)
{
    // callback_source asks for nothing beyond the size, and calls one read at a time.
    return std::make_unique<callback_source>(size, [parts = std::move(parts), reads = std::move(reads)](
                                                       std::uint64_t offset, std::span<std::byte> destination) {
        if (reads) {
            reads->push_back(offset);
        }
        for (std::size_t i = 0; i < destination.size(); ++i) {
            destination[i] = virtual_byte(offset + i);
        }
        const std::uint64_t end = offset + destination.size();
        for (const placed_bytes& part : parts) {
            const std::uint64_t first = std::max(offset, part.position);
            const std::uint64_t last = std::min(end, part.position + part.data.size());
            for (std::uint64_t at = first; at < last; ++at) {
                destination[at - offset] = part.data[at - part.position];
            }
        }
    });
}

} // namespace openxisf::test
