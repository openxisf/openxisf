// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/io.h>

#include "support/fixture_builder.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Files of several GiB without memory or disk space: sources whose bytes are computed from their offset, but for the
// parts placed at chosen offsets, such as a header, so that the offsets above 4 GiB can be tested anywhere.

namespace openxisf::test {

/// The byte at offset of a virtual file: a function of every bit of the offset, so that a read at an offset cut to 32
/// bits gets other bytes.
[[nodiscard]] constexpr std::byte virtual_byte(std::uint64_t offset) noexcept
{
    return static_cast<std::byte>((offset + ((offset >> 32U) * 7U)) % 251U);
}

/// The size bytes of a virtual file that start at offset.
[[nodiscard]] std::vector<std::byte> virtual_bytes(std::uint64_t offset, std::size_t size);

/// A source of size bytes that holds each of parts at its position and virtual_byte() at every other offset: only the
/// parts take memory. It records the offset of each read in reads, when that is given.
[[nodiscard]] std::unique_ptr<input_source> virtual_source(std::uint64_t size, std::vector<placed_bytes> parts,
                                                           std::shared_ptr<std::vector<std::uint64_t>> reads = nullptr);

} // namespace openxisf::test
