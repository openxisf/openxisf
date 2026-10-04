// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace openxisf::test {

/// Creates a file of size bytes that takes almost no disk space: it reads as zeros except where bytes are written into
/// it. Returns false where the file system cannot create one.
[[nodiscard]] bool create_sparse_file(const std::filesystem::path& path, std::uint64_t size);

/// Overwrites bytes of an existing file at offset.
void write_at(const std::filesystem::path& path, std::uint64_t offset, std::span<const std::byte> data);

} // namespace openxisf::test
