// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "crypto/hash.h"

#include <openxisf/error.h>

#include "core/data_encoding.h"
#include "support/bytes.h"
#include "support/environment.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using openxisf::detail::compute_digest;
using openxisf::detail::encode_hex;
using openxisf::detail::hash_algorithm;
using openxisf::detail::hash_backend;
using openxisf::detail::hash_name;
using openxisf::detail::hasher;

constexpr std::array all_algorithms{hash_algorithm::sha1, hash_algorithm::sha256, hash_algorithm::sha512,
                                    hash_algorithm::sha3_256, hash_algorithm::sha3_512};

// The backends of the build: the portable code, and libcrypto in a build with OpenSSL. Every test runs on each.
std::vector<hash_backend> backends()
{
    std::vector<hash_backend> result{hash_backend::portable};
    if (openxisf::detail::default_hash_backend() == hash_backend::libcrypto) {
        result.push_back(hash_backend::libcrypto);
    }
    return result;
}

std::string backend_name(hash_backend backend)
{
    return backend == hash_backend::portable ? "portable" : "libcrypto";
}

std::string digest_of(hash_algorithm algorithm, std::span<const std::byte> message, hash_backend backend)
{
    hasher state(algorithm, backend);
    state.update(message);
    return encode_hex(state.finish());
}

// The messages of the examples of FIPS 180 and FIPS 202.
enum class message
{
    empty,
    abc,
    two_blocks_448,
    two_blocks_896,
    a3_1600,
    million_a,
};

std::vector<std::byte> make(message which)
{
    switch (which) {
    case message::empty:
        return {};
    case message::abc:
        return openxisf::test::bytes("abc");
    case message::two_blocks_448:
        return openxisf::test::bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    case message::two_blocks_896:
        return openxisf::test::bytes(
            "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopq"
            "rlmnopqrsmnopqrstnopqrstu");
    case message::a3_1600:
        return std::vector<std::byte>(200, std::byte{0xA3});
    case message::million_a:
        return std::vector<std::byte>(1'000'000, std::byte{'a'});
    }
    return {};
}

struct known_answer
{
    hash_algorithm algorithm{};
    message input{};
    std::string_view digest{};
};

// The NIST examples, also checked against an independent implementation.
constexpr std::array known_answers{
    known_answer{.algorithm = hash_algorithm::sha1,
                 .input = message::empty,
                 .digest = "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
    known_answer{
        .algorithm = hash_algorithm::sha1, .input = message::abc, .digest = "a9993e364706816aba3e25717850c26c9cd0d89d"},
    known_answer{.algorithm = hash_algorithm::sha1,
                 .input = message::two_blocks_448,
                 .digest = "84983e441c3bd26ebaae4aa1f95129e5e54670f1"},
    known_answer{.algorithm = hash_algorithm::sha1,
                 .input = message::million_a,
                 .digest = "34aa973cd4c4daa4f61eeb2bdbad27316534016f"},
    known_answer{.algorithm = hash_algorithm::sha256,
                 .input = message::empty,
                 .digest = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    known_answer{.algorithm = hash_algorithm::sha256,
                 .input = message::abc,
                 .digest = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
    known_answer{.algorithm = hash_algorithm::sha256,
                 .input = message::two_blocks_448,
                 .digest = "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    known_answer{.algorithm = hash_algorithm::sha256,
                 .input = message::million_a,
                 .digest = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
    known_answer{.algorithm = hash_algorithm::sha512,
                 .input = message::empty,
                 .digest = "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
                           "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"},
    known_answer{.algorithm = hash_algorithm::sha512,
                 .input = message::abc,
                 .digest = "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                           "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"},
    known_answer{.algorithm = hash_algorithm::sha512,
                 .input = message::two_blocks_896,
                 .digest = "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
                           "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909"},
    known_answer{.algorithm = hash_algorithm::sha512,
                 .input = message::million_a,
                 .digest = "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973eb"
                           "de0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b"},
    known_answer{.algorithm = hash_algorithm::sha3_256,
                 .input = message::empty,
                 .digest = "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a"},
    known_answer{.algorithm = hash_algorithm::sha3_256,
                 .input = message::abc,
                 .digest = "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"},
    known_answer{.algorithm = hash_algorithm::sha3_256,
                 .input = message::a3_1600,
                 .digest = "79f38adec5c20307a98ef76e8324afbfd46cfd81b22e3973c65fa1bd9de31787"},
    known_answer{.algorithm = hash_algorithm::sha3_256,
                 .input = message::million_a,
                 .digest = "5c8875ae474a3634ba4fd55ec85bffd661f32aca75c6d699d0cdcb6c115891c1"},
    known_answer{.algorithm = hash_algorithm::sha3_512,
                 .input = message::empty,
                 .digest = "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a6"
                           "15b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26"},
    known_answer{.algorithm = hash_algorithm::sha3_512,
                 .input = message::abc,
                 .digest = "b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e"
                           "10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0"},
    known_answer{.algorithm = hash_algorithm::sha3_512,
                 .input = message::a3_1600,
                 .digest = "e76dfad22084a8b1467fcf2ffa58361bec7628edf5f3fdc0e4805dc48caeeca8"
                           "1b7c13c30adf52a3659584739a2df46be589c51ca1a4a8416df6545a1ce8ba00"},
    known_answer{.algorithm = hash_algorithm::sha3_512,
                 .input = message::million_a,
                 .digest = "3c3a876da14034ab60627c077bb98f7e120a2a5370212dffb3385a18d4f38859"
                           "ed311d0a9d5141ce9cc5c66ee689b266a8aa18ace8282a0e0db596c90b0a7b87"},
};

TEST(hash, computes_the_nist_examples)
{
    for (const hash_backend backend : backends()) {
        for (const known_answer& answer : known_answers) {
            EXPECT_EQ(digest_of(answer.algorithm, make(answer.input), backend), answer.digest)
                << hash_name(answer.algorithm) << ", message " << static_cast<int>(answer.input) << ", "
                << backend_name(backend);
        }
    }
}

// Messages of n bytes with the values 0, 1, 2, ..., whose padding falls on each side of a block boundary: 55 and 56
// bytes are the last lengths with and without room for the length field of SHA-1 and SHA-256 (111 and 112 for
// SHA-512), and the rates of SHA3-256 and SHA3-512 are 136 and 72 bytes. The digests come from an independent
// implementation.
TEST(hash, pads_the_message_on_both_sides_of_a_block_boundary)
{
    struct boundary_case
    {
        hash_algorithm algorithm{};
        std::size_t length = 0;
        std::string_view digest{};
    };
    constexpr std::array cases{
        boundary_case{
            .algorithm = hash_algorithm::sha1, .length = 55, .digest = "8ae2d46729cfe68ff927af5eec9c7d1b66d65ac2"},
        boundary_case{
            .algorithm = hash_algorithm::sha1, .length = 56, .digest = "636e2ec698dac903498e648bd2f3af641d3c88cb"},
        boundary_case{
            .algorithm = hash_algorithm::sha1, .length = 63, .digest = "6d942da0c4392b123528f2905c713a3ce28364bd"},
        boundary_case{
            .algorithm = hash_algorithm::sha1, .length = 64, .digest = "c6138d514ffa2135bfce0ed0b8fac65669917ec7"},
        boundary_case{
            .algorithm = hash_algorithm::sha1, .length = 65, .digest = "69bd728ad6e13cd76ff19751fde427b00e395746"},
        boundary_case{.algorithm = hash_algorithm::sha256,
                      .length = 55,
                      .digest = "463eb28e72f82e0a96c0a4cc53690c571281131f672aa229e0d45ae59b598b59"},
        boundary_case{.algorithm = hash_algorithm::sha256,
                      .length = 56,
                      .digest = "da2ae4d6b36748f2a318f23e7ab1dfdf45acdc9d049bd80e59de82a60895f562"},
        boundary_case{.algorithm = hash_algorithm::sha256,
                      .length = 63,
                      .digest = "29af2686fd53374a36b0846694cc342177e428d1647515f078784d69cdb9e488"},
        boundary_case{.algorithm = hash_algorithm::sha256,
                      .length = 64,
                      .digest = "fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108"},
        boundary_case{.algorithm = hash_algorithm::sha256,
                      .length = 65,
                      .digest = "4bfd2c8b6f1eec7a2afeb48b934ee4b2694182027e6d0fc075074f2fabb31781"},
        boundary_case{.algorithm = hash_algorithm::sha512,
                      .length = 111,
                      .digest = "a1a111449b198d9b1f538bad7f3fc1022b3a5b1a5e90a0bc860de8512746cbc3"
                                "1599e6c834de3a3235327af0b51ff57bf7acf1974a73014d9c3953812edc7c8d"},
        boundary_case{.algorithm = hash_algorithm::sha512,
                      .length = 112,
                      .digest = "c5fbd731d19d2ae1180f001be72c2c1aaba1d7b094b3748880e24593b8e117a7"
                                "50e11c1bd867cc2f96dace8c8b74abd2d5c4f236be444e77d30d1916174070b9"},
        boundary_case{.algorithm = hash_algorithm::sha512,
                      .length = 127,
                      .digest = "eab89674feaa34e27aebeeff3c0a4d70070bb872d5e9f186cf1dbbdee517b6e3"
                                "5724d629ff025a5b07185e911ada7e3c8acf830aa0e4f71777bd2d44f504f7f0"},
        boundary_case{.algorithm = hash_algorithm::sha512,
                      .length = 128,
                      .digest = "1dffd5e3adb71d45d2245939665521ae001a317a03720a45732ba1900ca3b835"
                                "1fc5c9b4ca513eba6f80bc7b1d1fdad4abd13491cb824d61b08d8c0e1561b3f7"},
        boundary_case{.algorithm = hash_algorithm::sha512,
                      .length = 129,
                      .digest = "1d9da57fbbdab09afb3506ab2d223d06109d65c1c8ad197f50138f714bc4c3f2"
                                "fe5787922639c680acad1c651f955990425954ce2cba0c5cc83f2667d878eb0f"},
        boundary_case{.algorithm = hash_algorithm::sha3_256,
                      .length = 135,
                      .digest = "fded8fd9d6551c601eeb3b7c6bc5e5cfd8aad1d015b7e9aaa9c9b9475231d5e2"},
        boundary_case{.algorithm = hash_algorithm::sha3_256,
                      .length = 136,
                      .digest = "cf3ccff92480a29160c2d38317c430e14749bfee1788106957dfe73f8c4930e5"},
        boundary_case{.algorithm = hash_algorithm::sha3_256,
                      .length = 137,
                      .digest = "ce9d7dc90913ee5d92745019479a5352c6d6279bef18ed07dc0a83ee8084daca"},
        boundary_case{.algorithm = hash_algorithm::sha3_512,
                      .length = 71,
                      .digest = "3ccc850d53a1287af7b4560b2ef0d43eb5d9a80d62a0e9cf1dbc040135921104"
                                "d4395168e90bfc871773ebb34bca1bd67056e1cc7dc7a48ff7c3167d389f117c"},
        boundary_case{.algorithm = hash_algorithm::sha3_512,
                      .length = 72,
                      .digest = "5d63f2bbe971a983ac6847480106e4e1264ee3a0befd79954914e1d86e795b2e"
                                "18238f12fc5e46cb9cc78efdec610a93647cc04e1c23d8caaa6a58c21dd26c07"},
        boundary_case{.algorithm = hash_algorithm::sha3_512,
                      .length = 73,
                      .digest = "921d9b7b2b0f3066a1646dbb058c979cb3925dec0f8c269faaa7f9648e73465a"
                                "e55ec527257d5d5e1cfdbf5d6799bea1004b6186f5108c74e3b92fe924166558"},
    };
    for (const hash_backend backend : backends()) {
        for (const boundary_case& entry : cases) {
            std::vector<std::byte> input(entry.length);
            for (std::size_t i = 0; i < input.size(); ++i) {
                input[i] = static_cast<std::byte>(i);
            }
            EXPECT_EQ(digest_of(entry.algorithm, input, backend), entry.digest)
                << hash_name(entry.algorithm) << ", " << entry.length << " bytes, " << backend_name(backend);
        }
    }
}

TEST(hash, the_digest_does_not_depend_on_how_the_message_is_cut)
{
    const std::vector<std::byte> input = openxisf::test::pattern(1000);
    for (const hash_backend backend : backends()) {
        for (const hash_algorithm algorithm : all_algorithms) {
            const std::string whole = digest_of(algorithm, input, backend);
            for (const std::size_t piece : {1U, 7U, 63U, 64U, 65U, 72U, 136U, 999U}) {
                hasher state(algorithm, backend);
                for (std::size_t start = 0; start < input.size(); start += piece) {
                    state.update(std::span(input).subspan(start, std::min(piece, input.size() - start)));
                }
                state.update({});
                EXPECT_EQ(encode_hex(state.finish()), whole)
                    << hash_name(algorithm) << ", pieces of " << piece << ", " << backend_name(backend);
            }
        }
    }
}

TEST(hash, hashes_a_message_of_more_than_4_gib_in_one_piece)
{
    // The message holds 2^32 + 7 bytes of the test pattern, given in a single update. Like the large samples, this runs
    // only where OPENXISF_LARGE_SAMPLES_DIR is set, since it takes 4 GiB of memory. The digests come from hashlib of
    // Python.
    if (!openxisf::test::environment_variable("OPENXISF_LARGE_SAMPLES_DIR")) {
        GTEST_SKIP() << "OPENXISF_LARGE_SAMPLES_DIR is not set";
    }
    struct expectation
    {
        hash_algorithm algorithm{};
        std::string_view digest{};
    };
    constexpr std::array expected{
        expectation{.algorithm = hash_algorithm::sha1, .digest = "22276a13dc95a0c3c894ef70514eacdd40ec29ea"},
        expectation{.algorithm = hash_algorithm::sha256,
                    .digest = "6bf766a835b5f5db2a13c00e3a5b870ef91637ee0f2d01b6f10a800f459eba59"},
        expectation{.algorithm = hash_algorithm::sha512,
                    .digest = "bc7cdb4d2be8f7f5a1341c84ad16cf2f4f38a39d590385c7b7b58ed7bf9c807f"
                              "b3c31d37b73f5bcefe63ac07d81785eb0c10da28d8e186b66bffb42c8bc5b576"},
        expectation{.algorithm = hash_algorithm::sha3_256,
                    .digest = "143bd90206361932f765eceed958088194a8b77f4559fcfbd52295233f4bbdc1"},
        expectation{.algorithm = hash_algorithm::sha3_512,
                    .digest = "2539c73b7fbf7d2869afa0c061b52013161b00dd46307778261b25645600d9ae"
                              "36894823cbfd7a28cade53c9dcd7c61057bc54945c36ebdf93e097d1c15e2cc2"},
    };
    const std::vector<std::byte> message = openxisf::test::pattern((std::size_t{1} << 32) + 7);
    for (const hash_backend backend : backends()) {
        for (const expectation& entry : expected) {
            EXPECT_EQ(digest_of(entry.algorithm, message, backend), entry.digest)
                << hash_name(entry.algorithm) << ", " << backend_name(backend);
        }
    }
}

TEST(hash, the_default_backend_is_that_of_the_build)
{
    // Digests come from libcrypto in a build with OpenSSL, which computes them faster.
#if defined(OPENXISF_WITH_OPENSSL)
    EXPECT_EQ(openxisf::detail::default_hash_backend(), hash_backend::libcrypto);
#else
    EXPECT_EQ(openxisf::detail::default_hash_backend(), hash_backend::portable);
    EXPECT_TRUE(openxisf::test::throws<openxisf::usage_error>(
        openxisf::errc::invalid_argument, [] { const hasher state(hash_algorithm::sha1, hash_backend::libcrypto); }));
#endif
    EXPECT_EQ(encode_hex(compute_digest(hash_algorithm::sha1, openxisf::test::bytes("abc"))),
              "a9993e364706816aba3e25717850c26c9cd0d89d");
}

} // namespace
