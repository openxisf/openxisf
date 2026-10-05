// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "benchmarks.h"

#include <benchmark/benchmark.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace openxisf::bench {

namespace {

// The value of the background at (x, y), plus noise from a linear congruential generator.
double sky(std::size_t x, std::size_t y, std::uint32_t& state)
{
    state = (state * 1'103'515'245U) + 12'345U;
    const double noise = static_cast<double>((state >> 8U) & 0xFFFFU) / 65536.0;
    return 0.1 + (0.05 * std::sin(static_cast<double>(x) / 200.0) * std::cos(static_cast<double>(y) / 150.0)) +
           (0.001 * noise);
}

template <typename Sample> void store(std::byte* out, Sample value)
{
    const auto bits = std::bit_cast<std::array<std::byte, sizeof(Sample)>>(value);
    for (std::size_t i = 0; i < bits.size(); ++i) {
        // Little-endian whatever the host.
        out[i] = bits[std::endian::native == std::endian::little ? i : bits.size() - 1 - i];
    }
}

} // namespace

std::vector<std::byte> sky_image(std::size_t width, std::size_t height, std::size_t channels, samples format)
{
    const std::size_t size = format == samples::float32 ? 4 : 2;
    std::vector<std::byte> image(width * height * channels * size);
    std::byte* out = image.data();
    std::uint32_t state = 1;
    for (std::size_t c = 0; c < channels; ++c) {
        for (std::size_t y = 0; y < height; ++y) {
            for (std::size_t x = 0; x < width; ++x) {
                const double value = sky(x, y, state) + (0.01 * static_cast<double>(c));
                if (format == samples::float32) {
                    store(out, static_cast<float>(value));
                } else {
                    store(out, static_cast<std::uint16_t>(std::lround(value * 65535.0)));
                }
                out += size;
            }
        }
    }
    return image;
}

const std::vector<std::byte>& small_sky(samples format)
{
    static const std::vector<std::byte> float32 = sky_image(1024, 1024, 1, samples::float32);
    static const std::vector<std::byte> uint16 = sky_image(1024, 1024, 1, samples::uint16);
    return format == samples::float32 ? float32 : uint16;
}

} // namespace openxisf::bench

int main(int argc, char** argv)
{
    openxisf::bench::register_block_benchmarks();
    openxisf::bench::register_unit_benchmarks();
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
