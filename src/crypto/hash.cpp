// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "crypto/hash.h"

#include <type_traits>

namespace openxisf::detail {

namespace {

std::variant<sha1, sha256, sha512, sha3> initial_state(hash_algorithm algorithm) noexcept
{
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

} // namespace

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

hasher::hasher(hash_algorithm algorithm) noexcept : state_(initial_state(algorithm)), algorithm_(algorithm) {}

void hasher::update(std::span<const std::byte> data)
{
    std::visit([data](auto& state) { state.update(data); }, state_);
}

std::vector<std::byte> hasher::finish()
{
    std::vector<std::byte> digest(digest_size(algorithm_));
    std::visit(
        [&digest](auto& state) {
            using algorithm_state = std::remove_cvref_t<decltype(state)>;
            if constexpr (std::is_same_v<algorithm_state, sha3>) {
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
