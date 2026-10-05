// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Benchmarks of the data block transforms: each codec with and without byte shuffling, byte shuffling alone, and the
// checksum algorithms, over the 1024 × 1024 sky.

#include <openxisf/limits.h>

#include "benchmarks.h"
#include "codec/compressed_block.h"
#include "codec/shuffle.h"
#include "crypto/hash.h"

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace openxisf::bench {

namespace {

const std::vector<std::byte>& image_of(std::size_t item_size)
{
    return small_sky(item_size == 2 ? samples::uint16 : samples::float32);
}

void count_bytes(benchmark::State& state, std::size_t size)
{
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(size));
}

void compress_image(benchmark::State& state, detail::compression_options options)
{
    const std::vector<std::byte>& image = image_of(options.item_size == 0 ? 4 : options.item_size);
    std::size_t compressed = 0;
    while (state.KeepRunning()) {
        const detail::compressed_block block = detail::compress_block(image, options);
        compressed = block.data.size();
        benchmark::DoNotOptimize(block.data.data());
    }
    count_bytes(state, image.size());
    state.counters["ratio"] = static_cast<double>(image.size()) / static_cast<double>(compressed);
}

void decompress_image(benchmark::State& state, detail::compression_options options)
{
    const std::vector<std::byte>& image = image_of(options.item_size == 0 ? 4 : options.item_size);
    const detail::compressed_block block = detail::compress_block(image, options);
    while (state.KeepRunning()) {
        const std::vector<std::byte> data = detail::decompress_block(block.data, block.compression, {});
        benchmark::DoNotOptimize(data.data());
    }
    count_bytes(state, image.size());
}

void shuffle_image(benchmark::State& state, std::size_t item_size, bool reverse, detail::shuffle_kernel kernel)
{
    const std::vector<std::byte>& image = image_of(item_size);
    std::vector<std::byte> output(image.size());
    while (state.KeepRunning()) {
        if (reverse) {
            detail::unshuffle_bytes(image, output, item_size, kernel);
        } else {
            detail::shuffle_bytes(image, output, item_size, kernel);
        }
        benchmark::DoNotOptimize(output.data());
    }
    count_bytes(state, image.size());
}

void hash_image(benchmark::State& state, detail::hash_algorithm algorithm, detail::hash_backend backend)
{
    const std::vector<std::byte>& image = image_of(4);
    while (state.KeepRunning()) {
        detail::hasher digest(algorithm, backend);
        digest.update(image);
        benchmark::DoNotOptimize(digest.finish());
    }
    count_bytes(state, image.size());
}

} // namespace

void register_block_benchmarks()
{
    const std::array codecs{
        std::pair{detail::compression_codec::zlib, "zlib"}, std::pair{detail::compression_codec::lz4, "lz4"},
        std::pair{detail::compression_codec::lz4hc, "lz4hc"}, std::pair{detail::compression_codec::zstd, "zstd"}};
    // Float32 samples without shuffling and with 4-byte items, and UInt16 samples with 2-byte items.
    for (const auto& [codec, name] : codecs) {
        for (const std::uint64_t item_size : {0U, 4U, 2U}) {
            const std::string label = std::string(name) + (item_size == 0 ? "" : "+sh" + std::to_string(item_size));
            const detail::compression_options options{.codec = codec, .item_size = item_size};
            benchmark::RegisterBenchmark("compress/" + label, compress_image, options)->Unit(benchmark::kMillisecond);
            benchmark::RegisterBenchmark("decompress/" + label, decompress_image, options)
                ->Unit(benchmark::kMillisecond);
        }
    }
    for (const auto& [kernel, kernel_name] :
         {std::pair{detail::shuffle_kernel::scalar, "scalar"}, std::pair{detail::shuffle_kernel::simd, "simd"}}) {
        for (const std::size_t item_size : {2U, 4U, 8U}) {
            const std::string label = std::string(kernel_name) + "/" + std::to_string(item_size);
            benchmark::RegisterBenchmark("shuffle/" + label, shuffle_image, item_size, false, kernel);
            benchmark::RegisterBenchmark("unshuffle/" + label, shuffle_image, item_size, true, kernel);
        }
    }
    // The portable code, and libcrypto in a build with OpenSSL.
    std::vector<std::pair<detail::hash_backend, std::string>> backends{{detail::hash_backend::portable, "portable"}};
    if (detail::default_hash_backend() == detail::hash_backend::libcrypto) {
        backends.emplace_back(detail::hash_backend::libcrypto, "libcrypto");
    }
    for (const auto& [backend, backend_name] : backends) {
        for (const auto& [algorithm, name] :
             {std::pair{detail::hash_algorithm::sha1, "sha1"}, std::pair{detail::hash_algorithm::sha256, "sha256"},
              std::pair{detail::hash_algorithm::sha512, "sha512"},
              std::pair{detail::hash_algorithm::sha3_256, "sha3-256"},
              std::pair{detail::hash_algorithm::sha3_512, "sha3-512"}}) {
            benchmark::RegisterBenchmark("hash/" + backend_name + "/" + name, hash_image, algorithm, backend)
                ->Unit(benchmark::kMillisecond);
        }
    }
}

} // namespace openxisf::bench
