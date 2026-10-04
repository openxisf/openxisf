// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "crypto/sha1.h"
#include "crypto/sha2.h"
#include "crypto/sha3.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

// The hashing algorithms of block checksums (spec §10.5).

namespace openxisf::detail {

/// The cryptographic hashing algorithms of spec §10.5, Table 9.
enum class hash_algorithm : std::uint8_t
{
    sha1,
    sha256,
    sha512,
    sha3_256,
    sha3_512,
};

/// The size of the digests of an algorithm, in bytes.
[[nodiscard]] std::size_t digest_size(hash_algorithm algorithm) noexcept;

/// The name of an algorithm for messages, such as "SHA-256".
[[nodiscard]] std::string_view hash_name(hash_algorithm algorithm) noexcept;

/// Computes the digest of a message given in pieces.
class hasher
{
public:
    explicit hasher(hash_algorithm algorithm) noexcept;

    void update(std::span<const std::byte> data);

    /// The digest of everything given to update(). The hasher cannot be used afterwards.
    [[nodiscard]] std::vector<std::byte> finish();

private:
    std::variant<sha1, sha256, sha512, sha3> state_;
    hash_algorithm algorithm_;
};

/// The digest of data.
[[nodiscard]] std::vector<std::byte> compute_digest(hash_algorithm algorithm, std::span<const std::byte> data);

} // namespace openxisf::detail
