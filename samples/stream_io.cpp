// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The C and C++ stream adapters of <openxisf/stream_io.h>. The program reads a unit from a FILE* and from a
// std::ifstream, copies it through a sink into a std::stringstream, and reads the copy back from that stream. The
// adapters are defined in the header, so the stream functions run in the C runtime and standard library of the
// application, and the library calls them from one thread at a time.
//
//   stream_io <file.xisf>

#include <openxisf/openxisf.h>
#include <openxisf/stream_io.h>

#include "arguments.h"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <share.h>
#endif

namespace {

// A C stream that closes itself.
struct stream_closer
{
    void operator()(std::FILE* stream) const noexcept
    {
        (void)std::fclose(stream); // NOLINT(cppcoreguidelines-owning-memory): the c_stream owns the stream
    }
};

using c_stream = std::unique_ptr<std::FILE, stream_closer>;

// Paths are UTF-8. std::fopen() and the narrow paths of std::ifstream take the ANSI code page on Windows, so the
// program opens files from their UTF-16 path there.
c_stream open_c_stream(const std::string& path)
{
#if defined(_WIN32)
    return c_stream(_wfsopen(example::to_wide(path).c_str(), L"rb", _SH_DENYNO));
#else
    return c_stream(std::fopen(path.c_str(), "rb")); // NOLINT(cppcoreguidelines-owning-memory): c_stream owns it
#endif
}

std::filesystem::path native_path(const std::string& path)
{
#if defined(_WIN32)
    return {example::to_wide(path)};
#else
    return {path};
#endif
}

// The pixels of every image of a unit, one after the other.
std::vector<std::byte> all_pixels(const openxisf::reader& unit)
{
    std::vector<std::byte> pixels;
    for (std::size_t i = 0; i < unit.images().size(); ++i) {
        const std::vector<std::byte> image = unit.read_pixels(i);
        pixels.insert(pixels.end(), image.begin(), image.end());
    }
    return pixels;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const std::vector<std::string> arguments = example::utf8_arguments(argc, argv);
        if (arguments.size() != 2) {
            std::cerr << "usage: stream_io <file.xisf>\n";
            return 2;
        }

        // A C stream, which must stay open while the reader uses it.
        const c_stream stream = open_c_stream(arguments[1]);
        if (!stream) {
            std::cerr << "cannot open " << arguments[1] << '\n';
            return 1;
        }
        std::vector<std::byte> from_c;
        {
            const openxisf::reader unit(std::make_unique<openxisf::stdio_source>(stream.get()));
            from_c = all_pixels(unit);
            std::cout << "FILE*: " << unit.images().size() << " images, " << from_c.size() << " bytes of pixels\n";
        }

        // A C++ stream, read through a source and copied through a sink into memory.
        std::ifstream input(native_path(arguments[1]), std::ios::binary);
        const openxisf::istream_source source(input);
        std::vector<std::byte> bytes(source.size());
        source.read(0, bytes);
        std::stringstream copy(std::ios::in | std::ios::out | std::ios::binary);
        openxisf::ostream_sink sink(copy);
        sink.write(bytes);
        sink.finish();

        const openxisf::reader unit(std::make_unique<openxisf::istream_source>(copy));
        const std::vector<std::byte> from_copy = all_pixels(unit);
        std::cout << "std::stringstream: " << unit.images().size() << " images, " << from_copy.size()
                  << " bytes of pixels\n";
        if (from_copy != from_c) {
            std::cerr << "the copy holds other pixels\n";
            return 1;
        }
        std::cout << "both streams hold the same pixels\n";
        return 0;
    } catch (const std::exception& failure) {
        (void)std::fputs(failure.what(), stderr);
        (void)std::fputs("\n", stderr);
        return 1;
    }
}
