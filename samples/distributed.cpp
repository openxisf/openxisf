// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Writes a distributed unit, a header file and a data blocks file next to it, and reads it back from the files. Then it
// writes the same unit into memory, as an application that keeps its files in a store of its own would, and reads it
// with a resolver that finds the data blocks file in that store.
//
//   distributed <file.xish>

#include <openxisf/openxisf.h>

#include "arguments.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t width = 256;
constexpr std::size_t height = 192;

// A gray ramp of 16-bit samples.
std::vector<std::uint16_t> render_ramp()
{
    std::vector<std::uint16_t> samples(width * height);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<std::uint16_t>(i % 65536);
    }
    return samples;
}

// Prints the images of a unit, and checks the pixels of the first one.
bool check(const openxisf::reader& file, const std::vector<std::uint16_t>& samples)
{
    for (const openxisf::diagnostic& entry : file.diagnostics()) {
        std::cout << "  " << entry.message << '\n';
    }
    std::cout << "  " << file.images().size() << " image, " << file.properties().size() << " property\n";
    return file.read_pixels<std::uint16_t>(0) == samples;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const std::vector<std::string> arguments = example::utf8_arguments(argc, argv);
        if (arguments.size() != 2) {
            std::cerr << "usage: distributed <file.xish>\n";
            return 2;
        }
        const std::string& path = arguments[1];
        const std::vector<std::uint16_t> samples = render_ramp();

        openxisf::image_info info;
        info.geometry = {.dimensions = {width, height}, .channels = 1};
        info.sample_format = openxisf::sample_format::uint16;
        info.color_space = openxisf::color_space::gray;
        openxisf::writer output({.creator_application = "distributed sample 1.0"});
        output.add_image(info, std::span(samples));
        output.properties().set("Sample:Calibration", openxisf::property_value(std::vector<double>(2000, 1.0)));

        // A header file, and a data blocks file of the same name ending with .xisb, which holds the pixel data and
        // the large property. The header locates them relative to its own directory, so both files can move together.
        output.save_distributed(path);
        std::cout << "wrote " << path << " and its data blocks file\n";

        // Opened from a path, a unit finds its data blocks files in the directory of its header file, and nowhere
        // else.
        const openxisf::reader from_files(path);
        if (!check(from_files, samples)) {
            std::cerr << "the pixels read back from the files differ\n";
            return 1;
        }

        // The same unit in memory: the header locates its data blocks file as path(@header_dir/blocks/unit.xisb).
        openxisf::memory_sink header;
        openxisf::memory_sink blocks;
        output.save_distributed(header, blocks, "blocks/unit.xisb");
        auto store = std::make_shared<std::map<std::string, std::vector<std::byte>>>();
        (*store)["blocks/unit.xisb"] = blocks.release();

        // The resolver opens what the header names, from the store; anything else is refused, and the blocks there
        // are unavailable.
        openxisf::read_options options;
        options.resolver =
            [store](const openxisf::external_reference& reference) -> std::unique_ptr<openxisf::input_source> {
            std::cout << "  resolving " << reference.location << '\n';
            const auto found = store->find(reference.location);
            if (reference.form != openxisf::location_form::relative_path || found == store->end()) {
                return nullptr;
            }
            return std::make_unique<openxisf::memory_source>(std::span<const std::byte>(found->second));
        };
        const openxisf::reader from_memory(std::make_unique<openxisf::memory_source>(header.release()), options);
        if (!check(from_memory, samples)) {
            std::cerr << "the pixels read back from memory differ\n";
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
