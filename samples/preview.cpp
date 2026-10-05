// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Makes an 8-bit preview of the first image of a unit, as an application shows an image: CIE L*a*b* samples are
// converted to RGB, the display function of the image stretches each nominal channel (one is computed with the adaptive
// algorithm of the specification when the image has none, or with --adaptive; --linked computes one for the three
// channels together, which keeps the colour balance), and the image is turned to its orientation, which is meant for
// showing it only. The preview is a binary PGM or PPM file (Netpbm), written with a file sink of the library.
//
//   preview [--adaptive] [--linked] <file.xisf> <preview.ppm>

#include <openxisf/openxisf.h>

#include "arguments.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct choices
{
    bool adaptive = false;
    bool linked = false;
    std::string input{};
    std::string output{};
};

std::optional<choices> parse(const std::vector<std::string>& arguments)
{
    choices result;
    std::vector<std::string> paths;
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        if (arguments[i] == "--adaptive") {
            result.adaptive = true;
        } else if (arguments[i] == "--linked") {
            result.linked = true;
        } else {
            paths.push_back(arguments[i]);
        }
    }
    if (paths.size() != 2) {
        return std::nullopt;
    }
    result.input = paths[0];
    result.output = paths[1];
    return result;
}

// What the preview shows: 8-bit samples of the nominal channels, interleaved, and the geometry of the image.
struct preview
{
    std::vector<std::byte> samples{};
    openxisf::geometry geometry{};
};

// The image stretched to 8 bits, from its samples of type T, interleaved.
template <openxisf::pixel_sample T> preview stretch(const openxisf::reader& file, const choices& choice)
{
    openxisf::image_info info = file.image(0);
    info.pixel_storage = openxisf::pixel_storage::normal;
    std::vector<T> samples = file.read_pixels<T>(0, {.storage = openxisf::pixel_storage::normal});

    // A decoder converts CIE L*a*b* samples to RGB with the RGB working space of the image (spec §8.5.4.1).
    if (info.color_space == openxisf::color_space::cie_lab) {
        openxisf::convert_lab_to_rgb(std::as_writable_bytes(std::span(samples)), info);
        info.color_space = openxisf::color_space::rgb;
    }

    const bool adaptive = choice.adaptive || !info.display_function;
    const openxisf::display_function function =
        adaptive
            ? openxisf::adaptive_display_function(std::as_bytes(std::span(samples)), info, {.linked = choice.linked})
            : info.display_function.value_or(openxisf::display_function{});
    std::cout << "display function: " << (adaptive ? "adaptive" : "the image's own")
              << (adaptive && choice.linked ? ", linked channels" : "") << '\n';

    // Each sample is mapped from the representable range to [0, 1], then stretched.
    const openxisf::bounds range = info.representable_range().value_or(openxisf::bounds{});
    const std::size_t channels = info.geometry.channels;
    const std::size_t nominal = openxisf::nominal_channels(info.color_space);
    preview result;
    result.geometry = {.dimensions = info.geometry.dimensions, .channels = nominal};
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const std::size_t channel = i % channels;
        if (channel < nominal) {
            const double x =
                std::clamp((static_cast<double>(samples[i]) - range.lower) / (range.upper - range.lower), 0.0, 1.0);
            const double shown = openxisf::apply_display_function(function, channel, x);
            result.samples.push_back(static_cast<std::byte>(std::lround(255.0 * shown)));
        }
    }
    return result;
}

preview stretch_of(const openxisf::reader& file, const choices& choice)
{
    switch (file.image(0).sample_format) {
    case openxisf::sample_format::uint8:
        return stretch<std::uint8_t>(file, choice);
    case openxisf::sample_format::uint16:
        return stretch<std::uint16_t>(file, choice);
    case openxisf::sample_format::uint32:
        return stretch<std::uint32_t>(file, choice);
    case openxisf::sample_format::uint64:
        return stretch<std::uint64_t>(file, choice);
    case openxisf::sample_format::float32:
        return stretch<float>(file, choice);
    case openxisf::sample_format::float64:
        return stretch<double>(file, choice);
    case openxisf::sample_format::complex32:
    case openxisf::sample_format::complex64:
        break;
    }
    throw std::runtime_error("complex images have no preview here");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const std::optional<choices> choice = parse(example::utf8_arguments(argc, argv));
        if (!choice) {
            std::cerr << "usage: preview [--adaptive] [--linked] <file.xisf> <preview.ppm>\n";
            return 2;
        }
        const openxisf::reader file(choice->input);
        if (file.images().empty() || file.image(0).geometry.dimensions.size() != 2) {
            std::cerr << "the unit has no two-dimensional image\n";
            return 1;
        }

        const preview stretched = stretch_of(file, *choice);
        const openxisf::orientation turn = file.image(0).orientation;
        const openxisf::geometry shown = openxisf::oriented_geometry(stretched.geometry, turn);
        const std::vector<std::byte> pixels =
            openxisf::orient_pixels(stretched.samples, stretched.geometry, 1, openxisf::pixel_storage::normal, turn);
        std::cout << "orientation: " << openxisf::orientation_name(turn) << ", " << shown.dimensions[0] << " x "
                  << shown.dimensions[1] << " pixels\n";

        const std::string header = std::string(shown.channels == 1 ? "P5" : "P6") + "\n" +
                                   std::to_string(shown.dimensions[0]) + " " + std::to_string(shown.dimensions[1]) +
                                   "\n255\n";
        openxisf::file_sink output(choice->output);
        output.write(std::as_bytes(std::span(header)));
        output.write(pixels);
        output.finish();
        std::cout << "wrote " << choice->output << '\n';
        return 0;
    } catch (const std::exception& failure) {
        (void)std::fputs(failure.what(), stderr);
        (void)std::fputs("\n", stderr);
        return 1;
    }
}
