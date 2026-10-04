// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/export.h>
#include <openxisf/image.h>
#include <openxisf/io.h>
#include <openxisf/property.h>
#include <openxisf/types.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// @file
/// Writing XISF units.

namespace openxisf {

/// The compression codecs of data blocks (spec §10.6.3 to §10.6.10).
enum class codec : std::uint8_t
{
    zlib,  ///< zlib (RFC 1950).
    lz4,   ///< LZ4, in its block format: the fastest.
    lz4hc, ///< LZ4 blocks written by LZ4 HC: smaller, written more slowly, read as fast.
    zstd,  ///< Zstandard: the codec that the specification recommends. Only decoders of Revision 1 read it.
};

/// The name of a codec in the specification: "zlib", "lz4", "lz4hc" or "zstd".
[[nodiscard]] constexpr std::string_view codec_name(codec value) noexcept
{
    switch (value) {
    case codec::zlib:
        return "zlib";
    case codec::lz4:
        return "lz4";
    case codec::lz4hc:
        return "lz4hc";
    case codec::zstd:
        return "zstd";
    }
    return {};
}

/// The hashing algorithms of data block checksums (spec §10.5, Table 9). Every decoder verifies SHA-1, SHA-256 and
/// SHA-512; SHA3-256 and SHA3-512 are optional for decoders, and some cannot read units that use them.
enum class checksum_algorithm : std::uint8_t
{
    sha1,
    sha256,
    sha512,
    sha3_256,
    sha3_512,
};

/// Options of a writer. Without compression, checksums and UUIDs, which are the defaults, every XISF 1.0 decoder reads
/// the units it writes.
struct write_options
{
    /// The application that creates the units: its name and version, such as "MyCapture 2.1"
    /// (XISF:CreatorApplication). Required.
    std::string creator_application{};
    /// When the units were created (XISF:CreationTime); the time of each save, to the millisecond, when empty. With a
    /// fixed time, a model is written the same way, byte for byte, every time.
    std::optional<date_time> creation_time{};
    /// Compress the data blocks with this codec; none when empty. A block that the codec does not make smaller is
    /// written uncompressed.
    std::optional<openxisf::codec> codec{};
    /// The compression level (spec §11.4.2): from 1, the fastest, to 100, the smallest, or 0 for the default of the
    /// codec.
    int compression_level = 0;
    /// Shuffle the bytes of each block of multibyte numbers before it is compressed (spec §10.6.2), which helps pixel
    /// data. It applies with a codec only.
    bool byte_shuffle = false;
    /// The largest piece of a compressed block, before compression, in bytes: a block is divided into subblocks of this
    /// size, which decoders can decompress in parallel. With 0, blocks are divided only where a codec requires it. A
    /// subblock that the codec does not make smaller is stored as it is, with equal compressed and uncompressed sizes,
    /// as PixInsight stores and reads such subblocks; decoders that know only the specification may not read them.
    std::uint64_t subblock_size = 0;
    /// Give each data block a checksum with this algorithm; none when empty.
    std::optional<checksum_algorithm> checksum{};
    /// Give each image without a UUID a new version 4 UUID.
    bool generate_uuids = false;
    /// Attached data blocks start at multiples of this many bytes, with zeros in between; 0 or 1 for none
    /// (XISF:BlockAlignmentSize). The blocks of a data blocks file start at multiples of it from the start of that
    /// file, and the header file of a distributed unit, which has no attached blocks, does not report it.
    std::uint16_t block_alignment = 4096;
    /// The data blocks of properties, table cells and ICC profiles of up to this many bytes are written in the header,
    /// in Base64 (XISF:MaxInlineBlockSize). Larger blocks, and pixel data, are attached after the header, or written to
    /// the data blocks file of a distributed unit.
    std::uint16_t max_inline_block_size = 3072;
    /// Flush a file to the storage device before it replaces its target, so that it survives a power failure. Slower.
    bool flush_to_disk = false;
    /// Called as a unit is written, and able to cancel the save. Its units of work are the bytes of the data blocks
    /// before compression: each block counts when it is written, and once more when its checksum or its compressed
    /// size must be known before the header is written, which a sink that cannot rewrite requires.
    progress_function progress{};
};

#if defined(_MSC_VER)
// The writer holds a standard-library member, which cl reports for an exported class. A C++ interface requires the
// same standard library on both sides of the DLL boundary anyway.
#pragma warning(push)
#pragma warning(disable : 4251)
#endif

/// Writes XISF units: a model of a unit, its metadata, images, standalone properties and tables, with the pixel data of
/// each image, as a monolithic file (spec §9.2) with save(), or as a distributed unit (spec §9.1.2) with
/// save_distributed(): a header file and a data blocks file, which the header locates relative to its own directory.
///
/// save() checks the whole model against the specification before it writes anything, and throws validation_error,
/// naming the object, for the first violation. The pixel data are borrowed: they must stay valid and unchanged until
/// the last save(). A writer is thread-compatible, like a standard container: save() can run on several threads at
/// once, and a change needs exclusive access.
class OPENXISF_API writer
{
public:
    /// A writer of units with the given options.
    /// @throws usage_error when options.compression_level is outside 0 to 100.
    explicit writer(write_options options);

    writer(const writer&) = delete;
    writer& operator=(const writer&) = delete;
    writer(writer&& other) noexcept;
    writer& operator=(writer&& other) noexcept;
    ~writer();

    [[nodiscard]] const write_options& options() const noexcept;

    /// The properties of the unit (spec §11.4), in the XISF namespace, such as XISF:Title. The writer writes
    /// XISF:CreationTime, XISF:CreatorApplication, XISF:CreatorModule, XISF:CreatorOS, XISF:MaxInlineBlockSize, and
    /// XISF:BlockAlignmentSize, XISF:ChecksumAlgorithms, XISF:CompressionCodecs and XISF:CompressionLevel when they
    /// apply, itself: it ignores the properties of these identifiers here, so that the metadata that a reader returns
    /// can be given back.
    [[nodiscard]] property_list& metadata() noexcept;
    [[nodiscard]] const property_list& metadata() const noexcept;

    /// The standalone properties (spec §11.1).
    [[nodiscard]] property_list& properties() noexcept;
    [[nodiscard]] const property_list& properties() const noexcept;

    /// The standalone table properties (spec §11.3).
    [[nodiscard]] std::vector<table>& tables() noexcept;
    [[nodiscard]] const std::vector<table>& tables() const noexcept;

    /// Adds an image after the others, with its pixel data: the samples in native byte order, in the storage model of
    /// info.pixel_storage, as reader::read_pixels() returns them. The writer keeps the span, not the data. Returns the
    /// index of the image.
    /// @throws usage_error when pixels does not have info.data_size() bytes.
    std::size_t add_image(image_info info, std::span<const std::byte> pixels);

    /// Adds an image with its samples as values of T, the type of its sample format (sample_format_of<T>()).
    /// @throws usage_error when T is another type, or samples does not hold the samples of the image.
    template <pixel_sample T> std::size_t add_image(image_info info, std::span<const T> samples)
    {
        check_sample_format(info, sample_format_of<T>());
        return add_image(std::move(info), std::as_bytes(samples));
    }

    /// The images added so far, in order.
    [[nodiscard]] std::span<const image_info> images() const noexcept;

    /// Writes the unit to the file at path, which is UTF-8 on every platform and ends with .xisf (spec §9.6). The file
    /// is replaced only once it is complete: a failed or cancelled save leaves the old file, or none.
    /// @throws usage_error when path is empty, not valid UTF-8, or does not end with .xisf in any case.
    /// @throws validation_error when the unit violates the specification; nothing is written then.
    /// @throws io_error when the file cannot be written.
    /// @throws cancelled_error when options().progress returns false.
    void save(std::string_view path) const;

    /// Writes the unit to sink, then calls its finish(), with the exceptions of the other save(). Exceptions of the
    /// sink pass through unchanged. A sink that can rewrite (output_sink::can_rewrite()) is written once, with the
    /// header written last; any other sink receives the unit in order, so compressed blocks are held in memory until
    /// the header is written.
    void save(output_sink& sink) const;

    /// Writes the unit as a distributed unit: a header file at path, which is UTF-8 on every platform and ends with
    /// .xish (spec §9.6), and a data blocks file of the same name ending with .xisb, which holds the data blocks that
    /// the header does not. The header locates each of them by the name of that file (`path(@header_dir/name.xisb)`),
    /// so the two files can move together. Both are replaced only once both are complete, the data blocks file first.
    /// The identifiers of the blocks are random, so a header file left with another data blocks file, after a failure
    /// between the two replacements, finds none of its blocks there.
    /// @throws usage_error when path is empty, not valid UTF-8, or does not end with .xish in any case, or when its
    ///         file name holds a control character or a backslash, which the header cannot locate.
    /// @throws validation_error, io_error or cancelled_error as the other save() does.
    void save_distributed(std::string_view path) const;

    /// Writes the unit as a distributed unit to two sinks, with the exceptions of the other save_distributed(): the
    /// header file to header, and the data blocks file to blocks, whose path from the directory of the header file is
    /// blocks_path, in UNIX syntax (spec §10.3), ending with .xisb. Both sinks are written completely before blocks,
    /// then header, is finished. A sink of blocks that can rewrite is written once, with the block index written last;
    /// any other sink of blocks receives the file in order, so compressed blocks are held in memory first.
    /// @throws usage_error when header and blocks are the same sink, or blocks_path is not a relative path in UNIX
    ///         syntax that ends with .xisb in any case: empty, absolute, with a . or .. step, a backslash, or a control
    ///         character.
    void save_distributed(output_sink& header, output_sink& blocks, std::string_view blocks_path) const;

private:
    static void check_sample_format(const image_info& info, sample_format format);

    struct state;
    std::unique_ptr<state> state_;
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace openxisf
