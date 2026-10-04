// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "crypto/sha2.h"

#include "core/endian.h"

#include <bit>

namespace openxisf::detail {

namespace {

// FIPS 180-4, §4.2.2: the first 32 bits of the fractional parts of the cube roots of the first 64 primes.
constexpr std::array<std::uint32_t, 64> sha256_constants{
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U, 0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
    0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U, 0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
    0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU, 0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
    0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U, 0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
    0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U, 0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
    0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U, 0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
    0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U, 0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
    0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U, 0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
};

// FIPS 180-4, §4.2.3: the first 64 bits of the fractional parts of the cube roots of the first 80 primes.
constexpr std::array<std::uint64_t, 80> sha512_constants{
    0x428A2F98D728AE22U, 0x7137449123EF65CDU, 0xB5C0FBCFEC4D3B2FU, 0xE9B5DBA58189DBBCU, 0x3956C25BF348B538U,
    0x59F111F1B605D019U, 0x923F82A4AF194F9BU, 0xAB1C5ED5DA6D8118U, 0xD807AA98A3030242U, 0x12835B0145706FBEU,
    0x243185BE4EE4B28CU, 0x550C7DC3D5FFB4E2U, 0x72BE5D74F27B896FU, 0x80DEB1FE3B1696B1U, 0x9BDC06A725C71235U,
    0xC19BF174CF692694U, 0xE49B69C19EF14AD2U, 0xEFBE4786384F25E3U, 0x0FC19DC68B8CD5B5U, 0x240CA1CC77AC9C65U,
    0x2DE92C6F592B0275U, 0x4A7484AA6EA6E483U, 0x5CB0A9DCBD41FBD4U, 0x76F988DA831153B5U, 0x983E5152EE66DFABU,
    0xA831C66D2DB43210U, 0xB00327C898FB213FU, 0xBF597FC7BEEF0EE4U, 0xC6E00BF33DA88FC2U, 0xD5A79147930AA725U,
    0x06CA6351E003826FU, 0x142929670A0E6E70U, 0x27B70A8546D22FFCU, 0x2E1B21385C26C926U, 0x4D2C6DFC5AC42AEDU,
    0x53380D139D95B3DFU, 0x650A73548BAF63DEU, 0x766A0ABB3C77B2A8U, 0x81C2C92E47EDAEE6U, 0x92722C851482353BU,
    0xA2BFE8A14CF10364U, 0xA81A664BBC423001U, 0xC24B8B70D0F89791U, 0xC76C51A30654BE30U, 0xD192E819D6EF5218U,
    0xD69906245565A910U, 0xF40E35855771202AU, 0x106AA07032BBD1B8U, 0x19A4C116B8D2D0C8U, 0x1E376C085141AB53U,
    0x2748774CDF8EEB99U, 0x34B0BCB5E19B48A8U, 0x391C0CB3C5C95A63U, 0x4ED8AA4AE3418ACBU, 0x5B9CCA4F7763E373U,
    0x682E6FF3D6B2B8A3U, 0x748F82EE5DEFB2FCU, 0x78A5636F43172F60U, 0x84C87814A1F0AB72U, 0x8CC702081A6439ECU,
    0x90BEFFFA23631E28U, 0xA4506CEBDE82BDE9U, 0xBEF9A3F7B2C67915U, 0xC67178F2E372532BU, 0xCA273ECEEA26619CU,
    0xD186B8C721C0C207U, 0xEADA7DD6CDE0EB1EU, 0xF57D4F7FEE6ED178U, 0x06F067AA72176FBAU, 0x0A637DC5A2C898A6U,
    0x113F9804BEF90DAEU, 0x1B710B35131C471BU, 0x28DB77F523047D84U, 0x32CAAB7B40C72493U, 0x3C9EBE0A15C9BEBCU,
    0x431D67C49C100D4CU, 0x4CC5D4BECB3E42B6U, 0x597F299CFC657E2AU, 0x5FCB6FAB3AD6FAECU, 0x6C44198C4A475817U,
};

// Ch and Maj of FIPS 180-4, §4.1.2 and §4.1.3.
template <typename Word> constexpr Word choose(Word x, Word y, Word z) noexcept
{
    return (x & y) ^ (~x & z);
}

template <typename Word> constexpr Word majority(Word x, Word y, Word z) noexcept
{
    return (x & y) ^ (x & z) ^ (y & z);
}

} // namespace

void sha256::update(std::span<const std::byte> data)
{
    blocks_.update(data, [this](std::span<const std::byte, 64> block) { compress(block); });
}

void sha256::finish(std::span<std::byte, digest_size> digest)
{
    blocks_.finish([this](std::span<const std::byte, 64> block) { compress(block); });
    for (std::size_t i = 0; i < state_.size(); ++i) {
        store_big_endian<std::uint32_t>(digest.subspan(4 * i).first<4>(), state_[i]);
    }
}

// FIPS 180-4, §6.2.2.
void sha256::compress(std::span<const std::byte, 64> block) noexcept
{
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t t = 0; t < 16; ++t) {
        schedule[t] = load_big_endian<std::uint32_t>(block.subspan(4 * t).first<4>());
    }
    for (std::size_t t = 16; t < 64; ++t) {
        const std::uint32_t w15 = schedule[t - 15];
        const std::uint32_t w2 = schedule[t - 2];
        const std::uint32_t sigma0 = std::rotr(w15, 7) ^ std::rotr(w15, 18) ^ (w15 >> 3U);
        const std::uint32_t sigma1 = std::rotr(w2, 17) ^ std::rotr(w2, 19) ^ (w2 >> 10U);
        schedule[t] = sigma1 + schedule[t - 7] + sigma0 + schedule[t - 16];
    }

    auto [a, b, c, d, e, f, g, h] = state_;
    for (std::size_t t = 0; t < 64; ++t) {
        const std::uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const std::uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const std::uint32_t t1 = h + sum1 + choose(e, f, g) + sha256_constants[t] + schedule[t];
        const std::uint32_t t2 = sum0 + majority(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void sha512::update(std::span<const std::byte> data)
{
    blocks_.update(data, [this](std::span<const std::byte, 128> block) { compress(block); });
}

void sha512::finish(std::span<std::byte, digest_size> digest)
{
    blocks_.finish([this](std::span<const std::byte, 128> block) { compress(block); });
    for (std::size_t i = 0; i < state_.size(); ++i) {
        store_big_endian<std::uint64_t>(digest.subspan(8 * i).first<8>(), state_[i]);
    }
}

// FIPS 180-4, §6.4.2.
void sha512::compress(std::span<const std::byte, 128> block) noexcept
{
    std::array<std::uint64_t, 80> schedule{};
    for (std::size_t t = 0; t < 16; ++t) {
        schedule[t] = load_big_endian<std::uint64_t>(block.subspan(8 * t).first<8>());
    }
    for (std::size_t t = 16; t < 80; ++t) {
        const std::uint64_t w15 = schedule[t - 15];
        const std::uint64_t w2 = schedule[t - 2];
        const std::uint64_t sigma0 = std::rotr(w15, 1) ^ std::rotr(w15, 8) ^ (w15 >> 7U);
        const std::uint64_t sigma1 = std::rotr(w2, 19) ^ std::rotr(w2, 61) ^ (w2 >> 6U);
        schedule[t] = sigma1 + schedule[t - 7] + sigma0 + schedule[t - 16];
    }

    auto [a, b, c, d, e, f, g, h] = state_;
    for (std::size_t t = 0; t < 80; ++t) {
        const std::uint64_t sum1 = std::rotr(e, 14) ^ std::rotr(e, 18) ^ std::rotr(e, 41);
        const std::uint64_t sum0 = std::rotr(a, 28) ^ std::rotr(a, 34) ^ std::rotr(a, 39);
        const std::uint64_t t1 = h + sum1 + choose(e, f, g) + sha512_constants[t] + schedule[t];
        const std::uint64_t t2 = sum0 + majority(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

} // namespace openxisf::detail
