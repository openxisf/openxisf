# OpenXISF

OpenXISF is a C++20 library for reading and writing XISF, the Extensible Image Serialization Format used by
PixInsight. It is written from the published specification and aims at full conformance with
[XISF 1.0, Revision 1](https://pixinsight.com/doc/docs/XISF-1.0-spec/XISF-1.0-spec.html), with an API that does not
depend on any application.

**Status: 0.2.0, an early release.** It reads and writes monolithic units; distributed units and the algorithms of
the specification come in later releases, and the API may change before 1.0. `openxisf::reader` checks the header of a
unit (the file structure, the XML, the root element, the elements of the specification and the references between
them) and its data blocks (where each one is, how it is encoded and compressed, and its checksum, which is verified with
SHA-1, SHA-256, SHA-512, SHA3-256 or SHA3-512 before the block is used). It reads the properties of a unit, of its
images and the standalone ones, with values of every type of the specification, and tables; its images: their geometry,
sample format, colour space and other attributes, and their pixel data, decompressed and in native byte order, in either
storage model, into memory of the library or of the application, with progress and cancellation; and what describes
each image: FITS keywords, ICC profile, RGB working space, display function, colour filter array, resolution and
thumbnail. Distributed units (external data blocks) are recognized but not read yet. `openxisf::writer` writes the
same model back: it checks a whole unit against the specification before it writes a byte of it, and writes its
metadata, images, properties and tables, with every block uncompressed and without checksums unless asked otherwise,
so that every XISF 1.0 decoder reads the result. On request it compresses blocks with zlib, LZ4, LZ4HC or Zstandard,
with byte shuffling and subblocks, adds checksums and generates UUIDs. Files are replaced only once they are complete.
The repository also has the build, test and packaging infrastructure, the error types and safety limits of the API, the
input and output layer, and the internal building blocks: the text forms of numbers and Boolean values, Base64 and
hexadecimal data, UUIDs and time stamps.

## Features and conformance

| Area | Reading | Writing |
|---|---|---|
| Monolithic units (`.xisf`) | Yes | Yes |
| Distributed units (`.xish` headers and `.xisb` blocks files) | Planned | Planned |
| Data blocks: attachment, embedded, inline | Yes | Attachment and inline |
| Data blocks: `url()` and `path()` | Planned | Planned |
| Checksums: SHA-1, SHA-256, SHA-512, SHA3-256, SHA3-512 | Yes | Yes |
| Compression: zlib, LZ4, LZ4HC, Zstandard, with byte shuffling and subblocks | Yes | Yes |
| Properties: scalars, complex numbers, strings, time points, vectors, matrices, tables | Yes | Yes |
| Images: every sample format, planar and normal storage, both byte orders | Yes | Yes, in little-endian byte order |
| Ancillary elements: FITS keywords, ICC profile, RGB working space, display function, color filter array, resolution, thumbnail | Yes | Yes |
| Colour transformations, display functions, orientation, property format rendering | Planned | Planned |
| Astrometric solutions | Planned | Planned |
| Signed units (the signature is returned, not verified) | Planned | Not applicable |

The table is updated at each release.

## Requirements and platforms

- C++20 and CMake 3.25 or newer. 64-bit targets only (x86-64 and arm64); configuring for a 32-bit toolchain stops with
  an error.
- Compilers: MSVC from Visual Studio 2022, clang-cl, GCC 11, Clang 15, and AppleClang from Xcode 15 with a macOS
  deployment target of 13.3 or later.
- Platforms: Windows (MSVC, clang-cl, MSYS2 UCRT64 and CLANG64), Linux (GCC and Clang, with libstdc++ or libc++) and macOS.
- Dependencies: pugixml, zlib, LZ4 and Zstandard. oneTBB and OpenSSL are optional. GoogleTest is needed for the tests,
  and Google Benchmark for the benchmarks.

## Building

The build is described by CMake presets: `msvc`, `clang-cl`, `msys2-ucrt64-gcc`, `msys2-clang64`, `linux-gcc`,
`linux-clang`, `linux-clang-libcxx` and `macos`, each with a `-debug` and a `-release` variant. The presets that do not
fit your host are hidden by `cmake --list-presets`.

```
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
ctest --preset linux-gcc-release
```

Binaries go to `out/build/<preset>`. Personal settings belong in a `CMakeUserPresets.json`, which git ignores.

Other presets run the tests under analysis tools: `linux-gcc-asan`, `linux-clang-asan` and `msvc-asan` (AddressSanitizer;
the Linux ones add UndefinedBehaviorSanitizer), `linux-clang-tsan` (ThreadSanitizer) and `linux-gcc-coverage`.
`linux-clang-fuzz` builds the fuzz targets with libFuzzer, and its tests fuzz each target for a minute, starting from the
seeds in `fuzz/seeds`. The other presets run those seeds once, as a regression test.

`-DOPENXISF_BUILD_BENCHMARKS=ON` builds `openxisf_benchmarks`, which measures the codecs, byte shuffling and the
checksums. Run it from a release build; its test only checks that every benchmark runs.

### Dependencies

- **vcpkg, manifest mode.** Set `VCPKG_ROOT` to a vcpkg checkout. The `msvc` and `clang-cl` presets use it
  automatically, and `vcpkg.json` pins the versions with a baseline. On other platforms add
  `-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`. Optional dependencies are features:
  `-DVCPKG_MANIFEST_FEATURES="tests;benchmarks;tbb;openssl"`.
- **Linux.** `sudo apt install pkg-config libpugixml-dev zlib1g-dev liblz4-dev libzstd-dev libgtest-dev`, and
  `libbenchmark-dev` for the benchmarks.
- **MSYS2.** In the UCRT64 or CLANG64 shell, install `cmake`, `ninja`, `pkgconf`, `pugixml`, `zlib`, `lz4`, `zstd` and
  `gtest` with the matching `mingw-w64-<environment>-` prefix, plus the compiler.
- **macOS.** `brew install pkgconf pugixml lz4 zstd googletest`, then configure with
  `-DCMAKE_PREFIX_PATH="$(brew --prefix)"`.

The Visual Studio presets use Ninja, which needs the compiler environment: run them from a Developer PowerShell
(or any shell where `cl` works). The MSYS2 presets run from the matching MSYS2 shell.

### Options

| Option | Default | Effect |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | Build a shared library instead of a static one |
| `OPENXISF_BUILD_TESTS` | `ON` at the top level | Build the test suites |
| `OPENXISF_BUILD_SAMPLES` | `ON` at the top level | Build the sample programs of `samples/`, which run as tests |
| `OPENXISF_BUILD_BENCHMARKS` | `OFF` | Build the benchmarks of `benchmarks/` with Google Benchmark |
| `OPENXISF_BUILD_FUZZERS` | `OFF` | Build the fuzz targets: libFuzzer programs when `OPENXISF_SANITIZE` contains `fuzzer`, seed replays otherwise (the presets turn it on) |
| `OPENXISF_FUZZ_SECONDS` | `60` | How long the test of each fuzz target runs with libFuzzer |
| `OPENXISF_BUILD_DOCS` | `OFF` | Add the `docs` target, which builds the API documentation with Doxygen |
| `OPENXISF_INSTALL` | `ON` at the top level | Generate the install rules |
| `OPENXISF_WITH_TBB` | `OFF` | Use oneTBB |
| `OPENXISF_WITH_OPENSSL` | `OFF` | Use OpenSSL (libcrypto) |
| `OPENXISF_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors (CI turns it on) |
| `OPENXISF_HARDENING` | `OFF` | Compile with hardening flags (the presets turn it on) |
| `OPENXISF_SANITIZE` | empty | Sanitizers for the library and the tests: `address;undefined`, `thread` or `fuzzer;address;undefined` (Clang) |

Optional dependencies are never detected on their own. Everything except the library is off when OpenXISF is added
with `add_subdirectory` or `FetchContent`.

## Using it from CMake

```cmake
find_package(openxisf CONFIG REQUIRED)
target_link_libraries(app PRIVATE openxisf::openxisf)
```

The installed package can be moved. A pkg-config file, `openxisf.pc`, is installed as well. OpenXISF is also set up to
become a vcpkg port.

## Examples

A unit is opened with `openxisf::reader`, from a UTF-8 path or from a source. The reader describes each image, and reads
its pixel data on request, into memory of its own or of the application, as they are stored or interleaved. This excerpt
of `samples/read_pixels.cpp` reads the samples of an image as values of its sample format, `T`, interleaved, while a
progress function reports how far the read is:

```cpp
#include <openxisf/openxisf.h>

const openxisf::reader file(path);
const openxisf::image_info& info = file.image(0);
std::vector<T> samples(info.geometry.sample_count());
file.read_pixels(0, std::span(samples),
                 {.storage = openxisf::pixel_storage::normal, .progress = progress_report()});
```

`samples/read_info.cpp` lists everything a unit holds: its metadata, its images with their properties, tables, FITS
keywords and the elements that describe them, and the problems the reader found in it, `file.diagnostics()`. The reader
loads the ICC profiles, the thumbnails and the values in data blocks when it opens a unit, within a configurable limit.
An application that only lists units can read their header alone, as `read_info --header-only` does, and load the rest
with `file.load_ancillary_data()` when it needs it:

```cpp
const bool header_only = arguments.size() == 3 && arguments[1] == "--header-only";
// Without the ancillary data, file.load_ancillary_data() would load them later.
const openxisf::reader file(arguments.back(), {.header_only = header_only});
```

A unit is written with `openxisf::writer`, from the same model that the reader returns, so that a unit read can be
written again. This excerpt of `samples/write_image.cpp` writes an image with properties, FITS keywords and a
thumbnail, compressed with Zstandard and byte shuffling, with SHA-256 checksums; without these options the writer
compresses nothing and adds no checksums, which every XISF 1.0 decoder reads, and Zstandard needs a decoder of
Revision 1 of the specification:

```cpp
openxisf::image_info info;
info.geometry = {.dimensions = {width, height}, .channels = 3};
info.sample_format = openxisf::sample_format::float32;
info.color_space = openxisf::color_space::rgb;
info.bounds = openxisf::bounds{.lower = 0.0, .upper = 1.0};
info.properties.set("Instrument:ExposureTime", 300.0F);
info.fits_keywords.push_back({.name = "EXPTIME", .value = "300.", .comment = "Exposure time in seconds"});
info.thumbnail = make_thumbnail(samples);

openxisf::writer output({.creator_application = "write_image sample 1.0",
                         .codec = openxisf::codec::zstd,
                         .byte_shuffle = true,
                         .checksum = openxisf::checksum_algorithm::sha256});
output.add_image(info, std::span(samples));
output.save(path);
```

The writer checks the whole unit before it writes anything, and throws `openxisf::validation_error`, which names the
element at fault, for anything that the specification forbids. The pixel data are borrowed until `save()`, which can
also write to any `openxisf::output_sink`.

Units are read from an `openxisf::input_source` and written to an `openxisf::output_sink`. The library provides them for
files (paths are UTF-8 on every platform, and a file is replaced only when it has been written completely), for memory,
for functions of the application, and, in `<openxisf/stream_io.h>`, for C and C++ streams (`samples/stream_io.cpp`). An
application can also implement them over its own storage; this is an excerpt of `samples/custom_io.cpp`, a source over
the function table of a storage library written in C:

```cpp
#include <openxisf/openxisf.h>

class blob_source final : public openxisf::input_source
{
public:
    blob_source(const blob_functions& functions, const void* blob) : functions_(functions), blob_(blob) {}

    std::uint64_t size() const override
    {
        return functions_.size(blob_);
    }

    void read(std::uint64_t offset, std::span<std::byte> destination) const override
    {
        if (functions_.read_at(blob_, offset, destination.data(), destination.size()) != 0) {
            throw openxisf::io_error(openxisf::errc::read_failed, "cannot read the blob", {}, {.offset = offset});
        }
    }

private:
    blob_functions functions_;
    const void* blob_;
};
```

A source reads exactly the bytes it is asked for, at the offset it is given, or throws. The library never asks for bytes
beyond its size, and calls it from one thread at a time unless it declares concurrent reads.

## Thread safety, errors and text

These are the design rules. They take effect as the API arrives.

- **Thread safety.** There is no mutable global state. The const member functions of a `reader` can be called from any
  number of threads at once, also to read pixels; `load_ancillary_data()`, the only one that changes it, needs exclusive
  access. Writers and values are thread-compatible, like standard containers. The file
  and memory sources can be read from any number of threads at once; the library calls a source that does not declare
  concurrent reads from one thread at a time.
- **Errors.** Failures are exceptions derived from `openxisf::error`, one class per kind of failure, each with an error
  code. A problem confined to one object of a unit, such as an unsupported codec, makes that object unavailable and is
  reported as a diagnostic, in `reader::diagnostics()`; the rest of the unit stays readable. Harmless deviations from
  the specification are diagnostics too. `read_options::strict` turns the problems that make an object unavailable into
  failures. A writer refuses a unit that violates the specification with `openxisf::validation_error`, before it
  writes anything. A long read or save can be cancelled from its progress function, which then throws
  `openxisf::cancelled_error`.
- **Text.** The API carries text as UTF-8 in `std::string` and `std::string_view`, paths included, on every platform.
  Invalid UTF-8 is rejected.

## Security

Report a vulnerability through GitHub's private vulnerability reporting, on the Security tab of the repository. Please do
not open a public issue for it.

Files are untrusted input. The design goals are: every size and offset is overflow-checked and every block is
bounds-checked; allocations are limited and configurable, and so are the size of the XML header, the nesting of its
elements and their number, and the window of Zstandard data; checksums are verified before data is used, compressed
data included, which are never decompressed when their checksum fails; decompression produces exactly the declared size
or fails; a document type declaration in the XML header is rejected; there is no network access; paths in distributed units stay inside the header's directory by default. They
are checked continuously by fuzzing, sanitizers and static analysis.

## Contributing

- Tests come first, with GoogleTest. Each behaviour is tested once, at the lowest level that proves it.
- A change to a parser, a codec or the I/O layer adds or extends a fuzz target, or adds a seed to `fuzz/seeds`. An input
  that once broke a target becomes a seed.
- Write plain code: classes and free functions, templates only where they remove real duplication. The naming follows
  the standard library: `snake_case` everywhere, lowercase enumerators, a trailing `_` on private members.
- Public headers carry short Doxygen comments. Every source file starts with an SPDX header, as the existing ones do;
  a file you create names you in its `SPDX-FileCopyrightText` line, and a file you change substantially can get a line
  of its own for you.
- Format with clang-format 22 and check with clang-tidy 22 (`pip install clang-format==22.1.3 clang-tidy==22.1.8`).
  Both run in CI.
- Sign off your commits to certify the [Developer Certificate of Origin](https://developercertificate.org/):
  `git commit -s`.
- Do not copy code from other XISF implementations that are under the GPL or the LGPL, or from the PixInsight Class
  Library.
- A pull request needs a green CI: every platform builds with warnings as errors, and the tests pass, also under the
  sanitizers.

## Author

OpenXISF is written and maintained by Ezequiel Ruiz. Contributors are credited in the history of the repository and in
the SPDX headers of the files they wrote.

## License

OpenXISF is licensed under the [Apache License 2.0](LICENSE); see also [NOTICE](NOTICE).

OpenXISF is an independent implementation. It is not affiliated with or endorsed by Pleiades Astrophoto S.L.
(PixInsight).
