// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

// LZ4 and LZ4HC (spec §10.6.5 to §10.6.8): the LZ4 block format, without the frame format. Both codecs write the same
// format, so one decoder serves both.

#include <openxisf/error.h>

#include "codec/codecs.h"
#include "core/checked_math.h"

#include <lz4.h>
#include <lz4hc.h>

#include <climits>
#include <string>

namespace openxisf::detail {

static_assert(lz4_max_input_size == LZ4_MAX_INPUT_SIZE);
static_assert(lz4_max_decoded_size == INT_MAX);
static_assert(lz4hc_default_level == LZ4HC_CLEVEL_DEFAULT);

namespace {

// The library gets a pointer to this byte for an empty buffer, never a null pointer. It reads no byte of an empty input
// and writes none to an empty output.
template <typename Byte, typename Char> Char* chars_of(std::span<Byte> data, std::byte& placeholder) noexcept
{
    return reinterpret_cast<Char*>(data.empty() ? &placeholder : data.data());
}

// Compresses input with compress(source, destination, size, capacity), which returns the compressed size, or 0 when it
// fails.
template <typename Compress>
void compress_with(Compress compress, std::span<const std::byte> input, std::vector<std::byte>& output)
{
    const int size = checked_cast<int>(input.size());
    const int bound = LZ4_compressBound(size);
    if (bound <= 0) {
        throw unsupported_error(errc::codec_failure,
                                "LZ4 cannot compress " + std::to_string(input.size()) + " bytes at once");
    }
    const std::size_t start = output.size();
    output.resize(start + static_cast<std::size_t>(bound));
    std::byte placeholder{};
    const int written = compress(chars_of<const std::byte, const char>(input, placeholder),
                                 reinterpret_cast<char*>(output.data() + start), size, bound);
    if (written <= 0) {
        throw unsupported_error(errc::codec_failure,
                                "LZ4 failed to compress " + std::to_string(input.size()) + " bytes");
    }
    output.resize(start + static_cast<std::size_t>(written));
}

} // namespace

void lz4_decompress(std::span<const std::byte> input, std::span<std::byte> output)
{
    const int expected = checked_cast<int>(output.size());
    std::byte placeholder{};
    const int decoded =
        LZ4_decompress_safe(chars_of<const std::byte, const char>(input, placeholder),
                            chars_of<std::byte, char>(output, placeholder), checked_cast<int>(input.size()), expected);
    if (decoded < 0) {
        throw integrity_error(errc::corrupt_compressed_data, "the LZ4 data are corrupt, or decode to more than " +
                                                                 std::to_string(expected) + " bytes");
    }
    if (decoded != expected) {
        throw integrity_error(errc::corrupt_compressed_data, "the LZ4 data decode to " + std::to_string(decoded) +
                                                                 " bytes, not " + std::to_string(expected));
    }
}

void lz4_compress(std::span<const std::byte> input, std::vector<std::byte>& output)
{
    compress_with(LZ4_compress_default, input, output);
}

void lz4hc_compress(std::span<const std::byte> input, int level, std::vector<std::byte>& output)
{
    compress_with([level](const char* source, char* destination, int size,
                          int capacity) { return LZ4_compress_HC(source, destination, size, capacity, level); },
                  input, output);
}

} // namespace openxisf::detail
