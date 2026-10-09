// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The adapters of the compression libraries (spec §10.6.3 to §10.6.10), one subblock at a time. A decoder fills its
// output exactly and consumes all of its input, or throws integrity_error with errc::corrupt_compressed_data; it never
// returns part of a subblock. An encoder appends to its output. Out of memory is std::bad_alloc; a library that fails
// for any other reason than the data, such as a version mismatch, throws unsupported_error with errc::codec_failure.
// Messages have no context; the caller adds it.

namespace openxisf::detail {

/// The default levels of the libraries, which the abstract level 0 selects.
inline constexpr int zlib_default_level = 6;
inline constexpr int lz4hc_default_level = 9;
inline constexpr int zstd_default_level = 3;

/// The largest input that LZ4 compresses, LZ4_MAX_INPUT_SIZE.
inline constexpr std::uint64_t lz4_max_input_size = 0x7E00'0000;

/// The largest compressed or decompressed subblock that the LZ4 decoder takes: the largest int.
inline constexpr std::uint64_t lz4_max_decoded_size = 0x7FFF'FFFF;

/// The most that zlib takes or gives in one call, which its 32-bit counters can hold.
inline constexpr std::size_t zlib_max_piece = 0xFFFF'FFFF;

/// Decompresses a zlib stream (RFC 1950), without a preset dictionary. Any size works: zlib gets the input and the
/// output in pieces of at most max_piece bytes, which only tests set lower.
void zlib_decompress(std::span<const std::byte> input, std::span<std::byte> output,
                     std::size_t max_piece = zlib_max_piece);

/// Compresses input into a zlib stream at a zlib level from 0 to 9, in pieces like zlib_decompress().
void zlib_compress(std::span<const std::byte> input, int level, std::vector<std::byte>& output,
                   std::size_t max_piece = zlib_max_piece);

/// Decompresses an LZ4 block (the block format, without a frame), as written by LZ4 and LZ4HC. Both sizes are at most
/// lz4_max_decoded_size.
void lz4_decompress(std::span<const std::byte> input, std::span<std::byte> output);

/// Compresses input, at most lz4_max_input_size bytes, into an LZ4 block.
void lz4_compress(std::span<const std::byte> input, std::vector<std::byte>& output);

/// Compresses input, at most lz4_max_input_size bytes, into an LZ4 block with LZ4HC at a level from 1 to 12.
void lz4hc_compress(std::span<const std::byte> input, int level, std::vector<std::byte>& output);

/// Decompresses a single Zstandard frame (RFC 8878). A frame that does not declare its content size is decoded through
/// a window buffer, whose size is limited to the largest power of two not above max_window (at least 1 KiB; 0 means
/// the largest that Zstandard supports); a larger window is a limit_error with errc::zstd_window_too_large. A frame
/// that declares its content size is decoded straight into output and needs no window buffer.
void zstd_decompress(std::span<const std::byte> input, std::span<std::byte> output, std::uint64_t max_window);

/// True when input starts with the header of a Zstandard frame that does not declare its content size, which
/// zstd_decompress() decodes through a window buffer. False for any other input, which it decodes in place or refuses.
[[nodiscard]] bool zstd_needs_window(std::span<const std::byte> input) noexcept;

/// Compresses input into a single Zstandard frame, which declares its content size, at a level from 1 to 22.
void zstd_compress(std::span<const std::byte> input, int level, std::vector<std::byte>& output);

} // namespace openxisf::detail
