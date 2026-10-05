// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "crypto/sha1.h"
#include "crypto/sha2.h"
#include "crypto/sha3.h"

#include <cstddef>
#include <cstdint>
#include <memory>
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

/// Where the digests come from. Both give the same digests.
enum class hash_backend : std::uint8_t
{
    /// The implementations of OpenXISF, written from FIPS 180-4 and FIPS 202.
    portable,
    /// libcrypto of OpenSSL, in a build with OPENXISF_WITH_OPENSSL.
    libcrypto,
};

/// The backend of hasher unless it is given another: libcrypto in a build with OPENXISF_WITH_OPENSSL, which computes
/// every digest faster, and the portable code otherwise.
[[nodiscard]] hash_backend default_hash_backend() noexcept;

/// Computes the digest of a message given in pieces.
class hasher
{
public:
    /// Throws usage_error with errc::invalid_argument for libcrypto in a build without it, and unsupported_error with
    /// errc::hash_failure when libcrypto fails.
    explicit hasher(hash_algorithm algorithm, hash_backend backend = default_hash_backend());
    hasher(const hasher&) = delete;
    hasher& operator=(const hasher&) = delete;
    hasher(hasher&& other) noexcept;
    hasher& operator=(hasher&& other) noexcept;
    ~hasher();

    void update(std::span<const std::byte> data);

    /// The digest of everything given to update(). The hasher cannot be used afterwards.
    [[nodiscard]] std::vector<std::byte> finish();

    [[nodiscard]] hash_algorithm algorithm() const noexcept
    {
        return algorithm_;
    }

private:
    // A digest of libcrypto, which only the source file knows.
    class libcrypto_state;
    using libcrypto_pointer = std::unique_ptr<libcrypto_state>;

    static std::variant<sha1, sha256, sha512, sha3, libcrypto_pointer> initial_state(hash_algorithm algorithm,
                                                                                     hash_backend backend);

    std::variant<sha1, sha256, sha512, sha3, libcrypto_pointer> state_;
    hash_algorithm algorithm_;
};

/// The digest of data, computed by the default backend.
[[nodiscard]] std::vector<std::byte> compute_digest(hash_algorithm algorithm, std::span<const std::byte> data);

} // namespace openxisf::detail
