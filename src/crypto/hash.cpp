// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "crypto/hash.h"

#include <openxisf/error.h>

#include <memory>
#include <new>
#include <string>
#include <type_traits>

#if defined(OPENXISF_WITH_OPENSSL)
#include <openssl/evp.h>
#endif

namespace openxisf::detail {

#if defined(OPENXISF_WITH_OPENSSL)

class hasher::libcrypto_state
{
public:
    explicit libcrypto_state(hash_algorithm algorithm) : context_(EVP_MD_CTX_new()), algorithm_(algorithm)
    {
        if (!context_) {
            throw std::bad_alloc();
        }
        check(EVP_DigestInit_ex(context_.get(), digest_of(algorithm), nullptr), algorithm);
    }

    void update(std::span<const std::byte> data)
    {
        check(EVP_DigestUpdate(context_.get(), data.data(), data.size()), algorithm_);
    }

    void finish(std::span<std::byte> digest)
    {
        unsigned int size = 0;
        check(EVP_DigestFinal_ex(context_.get(), reinterpret_cast<unsigned char*>(digest.data()), &size), algorithm_);
        if (size != digest.size()) {
            throw unsupported_error(errc::hash_failure, "libcrypto gave a " + std::string(hash_name(algorithm_)) +
                                                            " digest of " + std::to_string(size) + " bytes");
        }
    }

private:
    struct context_deleter
    {
        void operator()(EVP_MD_CTX* context) const noexcept
        {
            EVP_MD_CTX_free(context);
        }
    };

    static const EVP_MD* digest_of(hash_algorithm algorithm) noexcept
    {
        switch (algorithm) {
        case hash_algorithm::sha1:
            return EVP_sha1();
        case hash_algorithm::sha256:
            return EVP_sha256();
        case hash_algorithm::sha512:
            return EVP_sha512();
        case hash_algorithm::sha3_256:
            return EVP_sha3_256();
        case hash_algorithm::sha3_512:
            return EVP_sha3_512();
        }
        return nullptr;
    }

    // libcrypto returns 1 on success.
    static void check(int result, hash_algorithm algorithm)
    {
        if (result != 1) {
            throw unsupported_error(errc::hash_failure,
                                    "libcrypto failed to compute a " + std::string(hash_name(algorithm)) + " digest");
        }
    }

    std::unique_ptr<EVP_MD_CTX, context_deleter> context_;
    hash_algorithm algorithm_;
};

#else

// Never constructed: a build without OpenSSL refuses the libcrypto backend.
class hasher::libcrypto_state
{
public:
    void update(std::span<const std::byte> /*data*/) {}
    void finish(std::span<std::byte> /*digest*/) {}
};

#endif

std::size_t digest_size(hash_algorithm algorithm) noexcept
{
    switch (algorithm) {
    case hash_algorithm::sha1:
        return sha1::digest_size;
    case hash_algorithm::sha256:
    case hash_algorithm::sha3_256:
        return 32;
    case hash_algorithm::sha512:
    case hash_algorithm::sha3_512:
        return 64;
    }
    return 0;
}

std::string_view hash_name(hash_algorithm algorithm) noexcept
{
    switch (algorithm) {
    case hash_algorithm::sha1:
        return "SHA-1";
    case hash_algorithm::sha256:
        return "SHA-256";
    case hash_algorithm::sha512:
        return "SHA-512";
    case hash_algorithm::sha3_256:
        return "SHA3-256";
    case hash_algorithm::sha3_512:
        return "SHA3-512";
    }
    return {};
}

hash_backend default_hash_backend() noexcept
{
#if defined(OPENXISF_WITH_OPENSSL)
    return hash_backend::libcrypto;
#else
    return hash_backend::portable;
#endif
}

std::variant<sha1, sha256, sha512, sha3, hasher::libcrypto_pointer> hasher::initial_state(hash_algorithm algorithm,
                                                                                          hash_backend backend)
{
    if (backend == hash_backend::libcrypto) {
#if defined(OPENXISF_WITH_OPENSSL)
        return std::make_unique<libcrypto_state>(algorithm);
#else
        throw usage_error(errc::invalid_argument, "this build of OpenXISF has no libcrypto");
#endif
    }
    switch (algorithm) {
    case hash_algorithm::sha1:
        return sha1{};
    case hash_algorithm::sha256:
        return sha256{};
    case hash_algorithm::sha512:
        return sha512{};
    case hash_algorithm::sha3_256:
        return sha3(32);
    case hash_algorithm::sha3_512:
        return sha3(64);
    }
    return sha1{};
}

hasher::hasher(hash_algorithm algorithm, hash_backend backend)
    : state_(initial_state(algorithm, backend)), algorithm_(algorithm)
{}

hasher::hasher(hasher&& other) noexcept = default;
hasher& hasher::operator=(hasher&& other) noexcept = default;
hasher::~hasher() = default;

void hasher::update(std::span<const std::byte> data)
{
    std::visit(
        [data](auto& state) {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(state)>, libcrypto_pointer>) {
                state->update(data);
            } else {
                state.update(data);
            }
        },
        state_);
}

std::vector<std::byte> hasher::finish()
{
    std::vector<std::byte> digest(digest_size(algorithm_));
    std::visit(
        [&digest](auto& state) {
            using algorithm_state = std::remove_cvref_t<decltype(state)>;
            if constexpr (std::is_same_v<algorithm_state, libcrypto_pointer>) {
                state->finish(digest);
            } else if constexpr (std::is_same_v<algorithm_state, sha3>) {
                state.finish(digest);
            } else {
                state.finish(std::span<std::byte, algorithm_state::digest_size>(digest.data(), digest.size()));
            }
        },
        state_);
    return digest;
}

std::vector<std::byte> compute_digest(hash_algorithm algorithm, std::span<const std::byte> data)
{
    hasher state(algorithm);
    state.update(data);
    return state.finish();
}

} // namespace openxisf::detail
