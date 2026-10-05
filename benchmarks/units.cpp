// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Benchmarks of whole units, read from and written to memory or a file: opening a unit with a rich header, reading
// pixel data into memory of the caller against a raw read of the same bytes, converting between the storage models,
// reading blocks with checksums, and reading and writing a 100 MiB frame divided into subblocks, with its decompression
// and unshuffling alone. In a build with oneTBB, the conversions and the frame run in task arenas of several sizes, and
// their throughput counts wall time, not the processor time of the calling thread.

#include <openxisf/image.h>
#include <openxisf/io.h>
#include <openxisf/reader.h>
#include <openxisf/writer.h>

#include "benchmarks.h"
#include "codec/compressed_block.h"
#include "codec/shuffle.h"
#include "model/pixel_layout.h"

#include <benchmark/benchmark.h>

#if defined(OPENXISF_WITH_TBB)
#include <oneapi/tbb/task_arena.h>
#endif

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openxisf::bench {

namespace {

constexpr std::uint64_t mib = std::uint64_t{1} << 20;

// The frame: 5120 × 5120 Float32 samples, 100 MiB.
constexpr std::size_t frame_side = 5120;

// The colour image to convert: 3072 × 3072 pixels of three Float32 samples, 108 MiB.
constexpr std::size_t rgb_side = 3072;

const std::vector<std::byte>& frame()
{
    static const std::vector<std::byte> pixels = sky_image(frame_side, frame_side, 1, samples::float32);
    return pixels;
}

// The task arenas in which the parallel work runs: their numbers of threads.
std::vector<int> arena_sizes()
{
#if defined(OPENXISF_WITH_TBB)
    return {1, 8, 16};
#else
    return {1};
#endif
}

// Runs body in a oneTBB task arena of the given threads, where the library does its parallel work, or on this thread in
// a build without oneTBB.
template <typename Body> void in_arena(int threads, Body&& body)
{
#if defined(OPENXISF_WITH_TBB)
    oneapi::tbb::task_arena arena(threads);
    arena.execute(std::forward<Body>(body));
#else
    (void)threads;
    std::forward<Body>(body)();
#endif
}

void count_bytes(benchmark::State& state, std::uint64_t size)
{
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(size));
}

image_info gray_image(std::size_t width, std::size_t height, sample_format format)
{
    image_info info;
    info.geometry = {.dimensions = {width, height}, .channels = 1};
    info.sample_format = format;
    if (format == sample_format::float32) {
        info.bounds = bounds{.lower = 0.0, .upper = 1.0};
    }
    return info;
}

// How the pixel data of a unit are written.
struct encoding
{
    std::string name{};
    std::optional<openxisf::codec> codec{};
    std::uint64_t subblock_size = 0;
    std::optional<checksum_algorithm> checksum{};
};

write_options options_of(const encoding& how)
{
    return {.creator_application = "OpenXISF benchmarks 1.0",
            .codec = how.codec,
            .byte_shuffle = how.codec.has_value(),
            .subblock_size = how.subblock_size,
            .checksum = how.checksum};
}

std::vector<std::byte> write_unit(const image_info& info, std::span<const std::byte> pixels, const encoding& how)
{
    writer output(options_of(how));
    output.add_image(info, pixels);
    memory_sink sink;
    output.save(sink);
    return sink.release();
}

// A sink that keeps no bytes, so that a write measures the encoding of the unit, not the growth of a buffer.
class discarding_sink final : public output_sink
{
public:
    void write(std::span<const std::byte> data) override
    {
        position_ += data.size();
    }

    std::uint64_t position() const override
    {
        return position_;
    }

    bool can_rewrite() const override
    {
        return true;
    }

    void rewrite(std::uint64_t /*offset*/, std::span<const std::byte> /*data*/) override {}

private:
    std::uint64_t position_ = 0;
};

// A file in the directory of temporary files, removed when the program ends.
class temporary_file
{
public:
    temporary_file(const std::string& name, std::span<const std::byte> content)
        : path_(std::filesystem::temp_directory_path() / name)
    {
        std::ofstream output(path_, std::ios::binary);
        output.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
    }

    temporary_file(const temporary_file&) = delete;
    temporary_file& operator=(const temporary_file&) = delete;
    temporary_file(temporary_file&&) = delete;
    temporary_file& operator=(temporary_file&&) = delete;

    ~temporary_file()
    {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    // The path in UTF-8, as the library takes it.
    [[nodiscard]] std::string path() const
    {
        const std::u8string text = path_.u8string();
        return {text.begin(), text.end()};
    }

private:
    std::filesystem::path path_;
};

// The uncompressed unit of the frame, in memory and in a file.
const std::vector<std::byte>& frame_unit()
{
    static const std::vector<std::byte> unit =
        write_unit(gray_image(frame_side, frame_side, sample_format::float32), frame(), {});
    return unit;
}

const temporary_file& frame_file()
{
    static const temporary_file file("openxisf-benchmark-frame.xisf", frame_unit());
    return file;
}

// ---------------------------------------------------------------------------------------------------------------------
// Opening a unit

// A unit whose header has 3 × count properties, a third of them vectors in inline blocks, and count FITS keywords.
std::vector<std::byte> rich_unit(int count)
{
    image_info info = gray_image(64, 64, sample_format::uint16);
    for (int i = 0; i < count; ++i) {
        const std::string number = std::to_string(i);
        info.properties.set("Bench:Number" + number, 0.25 * i);
        info.properties.set("Bench:Text" + number, "The value of a property of some length, number " + number);
        info.properties.set("Bench:Vector" + number, std::vector<float>(8, 1.5F));
        info.fits_keywords.push_back({.name = "KEY" + number,
                                      .value = std::to_string(i * 3),
                                      .comment = "The comment of a keyword of some length"});
    }
    const std::vector<std::byte> pixels(std::size_t{64} * 64 * 2);
    return write_unit(info, pixels, {});
}

void open_unit(benchmark::State& state, int count)
{
    const std::vector<std::byte> unit = rich_unit(count);
    // The header length is the little-endian 32-bit number after the signature (spec §9.2).
    std::uint64_t header = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        header |= std::to_integer<std::uint64_t>(unit[8 + i]) << (8 * i);
    }
    while (state.KeepRunning()) {
        const reader file(std::make_unique<memory_source>(std::span(unit)));
        benchmark::DoNotOptimize(file.images().data());
    }
    count_bytes(state, header);
}

// ---------------------------------------------------------------------------------------------------------------------
// Uncompressed reads against raw reads

void read_into(benchmark::State& state, std::unique_ptr<input_source> source)
{
    const reader file(std::move(source));
    const std::size_t size = file.image(0).data_size();
    std::vector<std::byte> destination(size);
    while (state.KeepRunning()) {
        file.read_pixels(0, destination);
        benchmark::DoNotOptimize(destination.data());
    }
    count_bytes(state, size);
}

// The pixel data of the frame are its only attached block, at the end of the unit.
void read_raw(benchmark::State& state, const input_source& source)
{
    const std::size_t size = frame().size();
    std::vector<std::byte> destination(size);
    const std::uint64_t position = source.size() - size;
    while (state.KeepRunning()) {
        source.read(position, destination);
        benchmark::DoNotOptimize(destination.data());
    }
    count_bytes(state, size);
}

void read_frame_from_memory(benchmark::State& state)
{
    read_into(state, std::make_unique<memory_source>(std::span(frame_unit())));
}

void read_raw_from_memory(benchmark::State& state)
{
    read_raw(state, memory_source(std::span(frame_unit())));
}

void read_frame_from_file(benchmark::State& state)
{
    read_into(state, std::make_unique<file_source>(frame_file().path()));
}

void read_raw_from_file(benchmark::State& state)
{
    read_raw(state, file_source(frame_file().path()));
}

// ---------------------------------------------------------------------------------------------------------------------
// Storage conversion

void convert_samples(benchmark::State& state, samples format, pixel_storage from, std::size_t side, int threads)
{
    const std::vector<std::byte> source = sky_image(side, side, 3, format);
    std::vector<std::byte> destination(source.size());
    const std::size_t sample_size = format == samples::float32 ? 4 : 2;
    in_arena(threads, [&] {
        while (state.KeepRunning()) {
            detail::convert_storage(source, destination, from, side * side, 3, sample_size);
            benchmark::DoNotOptimize(destination.data());
        }
    });
    count_bytes(state, source.size());
}

// ---------------------------------------------------------------------------------------------------------------------
// Checksummed reads of the small sky

void read_checked(benchmark::State& state, const encoding& how)
{
    const std::vector<std::byte>& pixels = small_sky(samples::float32);
    const std::vector<std::byte> unit = write_unit(gray_image(1024, 1024, sample_format::float32), pixels, how);
    read_into(state, std::make_unique<memory_source>(std::span(unit)));
}

// ---------------------------------------------------------------------------------------------------------------------
// The frame in subblocks

void write_frame(benchmark::State& state, const encoding& how, int threads)
{
    const image_info info = gray_image(frame_side, frame_side, sample_format::float32);
    writer output(options_of(how));
    output.add_image(info, frame());
    std::uint64_t written = 0;
    in_arena(threads, [&] {
        while (state.KeepRunning()) {
            discarding_sink sink;
            output.save(sink);
            written = sink.position();
        }
    });
    count_bytes(state, frame().size());
    state.counters["ratio"] = static_cast<double>(frame().size()) / static_cast<double>(written);
}

void read_frame(benchmark::State& state, const encoding& how, int threads)
{
    const std::vector<std::byte> unit =
        write_unit(gray_image(frame_side, frame_side, sample_format::float32), frame(), how);
    const reader file(std::make_unique<memory_source>(std::span(unit)));
    std::vector<std::byte> destination(frame().size());
    in_arena(threads, [&] {
        while (state.KeepRunning()) {
            file.read_pixels(0, destination);
            benchmark::DoNotOptimize(destination.data());
        }
    });
    count_bytes(state, frame().size());
}

// The decompression alone, from stored bytes in memory into memory of the caller.
void decompress_frame(benchmark::State& state, std::uint64_t subblock_size, int threads)
{
    const detail::compressed_block block = detail::compress_block(
        frame(), {.codec = detail::compression_codec::zstd, .item_size = 4, .max_subblock_size = subblock_size});
    std::vector<std::byte> destination(frame().size());
    in_arena(threads, [&] {
        while (state.KeepRunning()) {
            detail::decompress_block_into(block.data, block.compression, {}, destination);
            benchmark::DoNotOptimize(destination.data());
        }
    });
    count_bytes(state, frame().size());
}

// Unshuffling the frame as a whole, in 4-byte items.
void unshuffle_frame(benchmark::State& state, int threads)
{
    std::vector<std::byte> destination(frame().size());
    in_arena(threads, [&] {
        while (state.KeepRunning()) {
            detail::unshuffle_bytes(frame(), destination, 4);
            benchmark::DoNotOptimize(destination.data());
        }
    });
    count_bytes(state, frame().size());
}

std::string subblock_label(std::uint64_t size)
{
    return size == 0 ? "whole" : "sub" + std::to_string(size / mib) + "M";
}

} // namespace

void register_unit_benchmarks()
{
    // Opening is linear in the size of the header: ten times the elements take about ten times as long.
    for (const int count : {500, 5000}) {
        benchmark::RegisterBenchmark("open/rich-header/" + std::to_string(count), open_unit, count)
            ->Unit(benchmark::kMicrosecond);
    }

    benchmark::RegisterBenchmark("frame/read/memory", read_frame_from_memory)->Unit(benchmark::kMillisecond);
    benchmark::RegisterBenchmark("frame/raw/memory", read_raw_from_memory)->Unit(benchmark::kMillisecond);
    benchmark::RegisterBenchmark("frame/read/file", read_frame_from_file)->Unit(benchmark::kMillisecond);
    benchmark::RegisterBenchmark("frame/raw/file", read_raw_from_file)->Unit(benchmark::kMillisecond);

    for (const int threads : arena_sizes()) {
        const std::string arena = "/arena" + std::to_string(threads);
        for (const auto& [format, name] : {std::pair{samples::uint16, "u16"}, std::pair{samples::float32, "f32"}}) {
            benchmark::RegisterBenchmark("convert/planar-to-normal/" + std::string(name) + "/1024" + arena,
                                         convert_samples, format, pixel_storage::planar, std::size_t{1024}, threads)
                ->Unit(benchmark::kMillisecond)
                ->UseRealTime();
            benchmark::RegisterBenchmark("convert/normal-to-planar/" + std::string(name) + "/1024" + arena,
                                         convert_samples, format, pixel_storage::normal, std::size_t{1024}, threads)
                ->Unit(benchmark::kMillisecond)
                ->UseRealTime();
        }
        benchmark::RegisterBenchmark("frame/convert/planar-to-normal/f32/" + std::to_string(rgb_side) + arena,
                                     convert_samples, samples::float32, pixel_storage::planar, rgb_side, threads)
            ->Unit(benchmark::kMillisecond)
            ->UseRealTime();
    }

    for (const auto& [compression, compression_name] :
         {std::pair{std::optional<openxisf::codec>{}, "none"}, std::pair{std::optional{codec::zstd}, "zstd+sh"}}) {
        for (const auto& [checksum, checksum_name] :
             {std::pair{std::optional<checksum_algorithm>{}, "none"},
              std::pair{std::optional{checksum_algorithm::sha1}, "sha1"},
              std::pair{std::optional{checksum_algorithm::sha256}, "sha256"},
              std::pair{std::optional{checksum_algorithm::sha512}, "sha512"},
              std::pair{std::optional{checksum_algorithm::sha3_256}, "sha3-256"},
              std::pair{std::optional{checksum_algorithm::sha3_512}, "sha3-512"}}) {
            const encoding how{.name = std::string(compression_name) + "/" + checksum_name,
                               .codec = compression,
                               .checksum = checksum};
            benchmark::RegisterBenchmark("read/sky/" + how.name, read_checked, how)->Unit(benchmark::kMillisecond);
        }
    }

    const std::vector<encoding> frame_encodings{
        {.name = "zstd+sh/" + subblock_label(0), .codec = codec::zstd},
        {.name = "zstd+sh/" + subblock_label(mib), .codec = codec::zstd, .subblock_size = mib},
        {.name = "zstd+sh/" + subblock_label(4 * mib), .codec = codec::zstd, .subblock_size = 4 * mib},
        {.name = "zstd+sh/" + subblock_label(16 * mib), .codec = codec::zstd, .subblock_size = 16 * mib},
        {.name = "lz4+sh/" + subblock_label(0), .codec = codec::lz4},
        {.name = "lz4+sh/" + subblock_label(4 * mib), .codec = codec::lz4, .subblock_size = 4 * mib},
        {.name = "zlib+sh/" + subblock_label(0), .codec = codec::zlib},
        {.name = "zlib+sh/" + subblock_label(4 * mib), .codec = codec::zlib, .subblock_size = 4 * mib},
    };
    for (const int threads : arena_sizes()) {
        const std::string arena = "/arena" + std::to_string(threads);
        for (const encoding& how : frame_encodings) {
            benchmark::RegisterBenchmark("frame/write/" + how.name + arena, write_frame, how, threads)
                ->Unit(benchmark::kMillisecond)
                ->UseRealTime();
            benchmark::RegisterBenchmark("frame/read/" + how.name + arena, read_frame, how, threads)
                ->Unit(benchmark::kMillisecond)
                ->UseRealTime();
        }
        // The decompression alone, and the unshuffling that follows it, which reads and writes the whole block.
        for (const std::uint64_t size : {std::uint64_t{0}, mib, 4 * mib}) {
            benchmark::RegisterBenchmark("frame/decompress/zstd+sh/" + subblock_label(size) + arena, decompress_frame,
                                         size, threads)
                ->Unit(benchmark::kMillisecond)
                ->UseRealTime();
        }
        benchmark::RegisterBenchmark("frame/unshuffle/4" + arena, unshuffle_frame, threads)
            ->Unit(benchmark::kMillisecond)
            ->UseRealTime();
    }
}

} // namespace openxisf::bench
