// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

// Benchmarks of the data block transforms: each codec with and without byte shuffling, byte shuffling alone, and the
// checksum algorithms, over a 4 MiB image. The numbers are what performance decisions rest on, so the data look like an
// astronomical image: a smooth background with a little noise, in Float32 and UInt16 samples.

#include <openxisf/limits.h>

#include "codec/compressed_block.h"
#include "codec/shuffle.h"
#include "crypto/hash.h"

#include <benchmark/benchmark.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace detail = openxisf::detail;

constexpr std::size_t width = 1024;
constexpr std::size_t height = 1024;

// The value of the background at (x, y), between about 0.05 and 0.15, plus noise of about 0.001.
double sky(std::size_t x, std::size_t y, std::uint32_t& state)
{
    state = (state * 1'103'515'245U) + 12'345U;
    const double noise = static_cast<double>((state >> 8U) & 0xFFFFU) / 65536.0;
    return 0.1 + (0.05 * std::sin(static_cast<double>(x) / 200.0) * std::cos(static_cast<double>(y) / 150.0)) +
           (0.001 * noise);
}

template <typename Sample> void store(std::vector<std::byte>& image, Sample value)
{
    const auto bits = std::bit_cast<std::array<std::byte, sizeof(Sample)>>(value);
    image.insert(image.end(), bits.begin(), bits.end());
}

std::vector<std::byte> make_image(bool floating_point)
{
    std::vector<std::byte> image;
    std::uint32_t state = 1;
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const double value = sky(x, y, state);
            if (floating_point) {
                store(image, static_cast<float>(value));
            } else {
                store(image, static_cast<std::uint16_t>(std::lround(value * 65535.0)));
            }
        }
    }
    return image;
}

const std::vector<std::byte>& image_of(std::size_t item_size)
{
    static const std::vector<std::byte> float32 = make_image(true);
    static const std::vector<std::byte> uint16 = make_image(false);
    return item_size == 2 ? uint16 : float32;
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

void shuffle_image(benchmark::State& state, std::size_t item_size, bool reverse)
{
    const std::vector<std::byte>& image = image_of(item_size);
    std::vector<std::byte> output(image.size());
    while (state.KeepRunning()) {
        if (reverse) {
            detail::unshuffle_bytes(image, output, item_size);
        } else {
            detail::shuffle_bytes(image, output, item_size);
        }
        benchmark::DoNotOptimize(output.data());
    }
    count_bytes(state, image.size());
}

void hash_image(benchmark::State& state, detail::hash_algorithm algorithm)
{
    const std::vector<std::byte>& image = image_of(4);
    while (state.KeepRunning()) {
        benchmark::DoNotOptimize(detail::compute_digest(algorithm, image));
    }
    count_bytes(state, image.size());
}

void register_benchmarks()
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
    for (const std::size_t item_size : {2U, 4U, 8U}) {
        benchmark::RegisterBenchmark("shuffle/" + std::to_string(item_size), shuffle_image, item_size, false);
        benchmark::RegisterBenchmark("unshuffle/" + std::to_string(item_size), shuffle_image, item_size, true);
    }
    for (const auto& [algorithm, name] :
         {std::pair{detail::hash_algorithm::sha1, "sha1"}, std::pair{detail::hash_algorithm::sha256, "sha256"},
          std::pair{detail::hash_algorithm::sha512, "sha512"}, std::pair{detail::hash_algorithm::sha3_256, "sha3-256"},
          std::pair{detail::hash_algorithm::sha3_512, "sha3-512"}}) {
        benchmark::RegisterBenchmark("hash/" + std::string(name), hash_image, algorithm)->Unit(benchmark::kMillisecond);
    }
}

} // namespace

int main(int argc, char** argv)
{
    register_benchmarks();
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
