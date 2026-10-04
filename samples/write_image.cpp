// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Writes an RGB image of 32-bit floating point samples, as a camera application would after a capture: properties of
// the observation, FITS keywords, a thumbnail, and its pixel data compressed with Zstandard and byte shuffling, with
// SHA-256 checksums. Then it reads the file back and checks the pixels.
//
//   write_image <file.xisf>

#include <openxisf/openxisf.h>

#include "arguments.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::size_t width = 640;
constexpr std::size_t height = 480;

// A synthetic exposure: a faint background gradient with one star, in [0, 1], planar: all the red samples, then the
// green ones, then the blue ones.
std::vector<float> render_sky()
{
    std::vector<float> samples(width * height * 3);
    for (std::size_t c = 0; c < 3; ++c) {
        for (std::size_t y = 0; y < height; ++y) {
            for (std::size_t x = 0; x < width; ++x) {
                const double dx = static_cast<double>(x) - 320.0;
                const double dy = static_cast<double>(y) - 200.0;
                const double star = 0.8 * std::exp(-((dx * dx) + (dy * dy)) / (2.0 * 9.0));
                const double background = 0.05 + (0.02 * static_cast<double>(y) / static_cast<double>(height)) +
                                          (0.01 * static_cast<double>(c));
                samples[(((c * height) + y) * width) + x] = static_cast<float>(std::min(1.0, background + star));
            }
        }
    }
    return samples;
}

// A thumbnail of 8-bit samples, a quarter of the size, by taking every fourth pixel.
openxisf::thumbnail make_thumbnail(const std::vector<float>& samples)
{
    openxisf::thumbnail result;
    result.geometry = {.dimensions = {width / 4, height / 4}, .channels = 3};
    result.color_space = openxisf::color_space::rgb;
    result.sample_format = openxisf::sample_format::uint8;
    for (std::size_t c = 0; c < 3; ++c) {
        for (std::size_t y = 0; y < height; y += 4) {
            for (std::size_t x = 0; x < width; x += 4) {
                const float value = samples[(((c * height) + y) * width) + x];
                result.pixels.push_back(static_cast<std::byte>(std::lround(value * 255.0F)));
            }
        }
    }
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const std::vector<std::string> arguments = example::utf8_arguments(argc, argv);
        if (arguments.size() != 2) {
            std::cerr << "usage: write_image <file.xisf>\n";
            return 2;
        }
        const std::string& path = arguments[1];
        const std::vector<float> samples = render_sky();

        // The image, its properties and what describes it.
        openxisf::image_info info;
        info.geometry = {.dimensions = {width, height}, .channels = 3};
        info.sample_format = openxisf::sample_format::float32;
        info.color_space = openxisf::color_space::rgb;
        info.bounds = openxisf::bounds{.lower = 0.0, .upper = 1.0};
        info.image_type = openxisf::image_type::light;
        info.properties.set("Instrument:ExposureTime", 300.0F);
        info.properties.set("Observation:Object:Name", "M31");
        info.properties.set("Observation:Time:Start", openxisf::to_date_time(std::chrono::sys_days{
                                                          std::chrono::year{2026} / std::chrono::October / 4}));
        info.fits_keywords.push_back({.name = "EXPTIME", .value = "300.", .comment = "Exposure time in seconds"});
        info.fits_keywords.push_back({.name = "OBJECT", .value = "'M31'", .comment = "Name of the object"});
        info.fits_keywords.push_back({.name = "HISTORY", .comment = "Written by the write_image sample"});
        info.thumbnail = make_thumbnail(samples);

        // Without options a writer uses no compression and no checksums, which every XISF 1.0 decoder reads.
        // Zstandard needs a decoder of Revision 1 of the specification.
        openxisf::writer output({.creator_application = "write_image sample 1.0",
                                 .codec = openxisf::codec::zstd,
                                 .byte_shuffle = true,
                                 .checksum = openxisf::checksum_algorithm::sha256});
        output.metadata().set("XISF:Title", "A synthetic exposure");
        output.add_image(info, std::span(samples));
        output.save(path);

        // The file as a reader sees it.
        const openxisf::reader file(path);
        const openxisf::image_info& written = file.image(0);
        std::cout << "wrote " << path << ": " << written.geometry.dimensions[0] << " x "
                  << written.geometry.dimensions[1] << " pixels, " << written.properties.size() << " properties, "
                  << written.fits_keywords.size() << " FITS keywords";
        if (const std::optional<openxisf::thumbnail>& small = written.thumbnail) {
            std::cout << ", a thumbnail of " << small->geometry.dimensions[0] << " x " << small->geometry.dimensions[1];
        }
        std::cout << '\n';
        if (file.read_pixels<float>(0) != samples) {
            std::cerr << "the pixels read back differ\n";
            return 1;
        }
        std::cout << "the pixels read back are those written\n";
        return 0;
    } catch (const std::exception& failure) {
        (void)std::fputs(failure.what(), stderr);
        (void)std::fputs("\n", stderr);
        return 1;
    }
}
