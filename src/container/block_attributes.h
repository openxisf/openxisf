// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "codec/compression.h"
#include "crypto/hash.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The values of the attributes that describe a data block (spec §10.3 to §10.6): location, the encoding of a Data
// element, byteOrder, checksum, compression and subblocks. Each parser checks the grammar of one value;
// describe_blocks() applies the rules that tie the attributes to each other and to the unit. Parsers throw
// invalid_data_error, or unsupported_error for a well-formed value that names a codec or an algorithm that OpenXISF
// does not implement. Their messages have no context; the caller adds it.

namespace openxisf::detail {

/// How the bytes of an inline or embedded block are written as text (spec §10.3).
enum class block_encoding : std::uint8_t
{
    base64,
    hex,
};

/// The forms of the location attribute (spec §10.3).
enum class location_kind : std::uint8_t
{
    inline_data, ///< inline:encoding, in the character data of the element.
    embedded,    ///< embedded, in the character data of a child Data element.
    attachment,  ///< attachment:position:size, in a monolithic file.
    url,         ///< url(URL) or url(URL):index-id, an external resource.
    path,        ///< path(file-path) or path(file-path):index-id, a local file.
};

/// True for the locations of external blocks, url() and path() (spec §10.2).
[[nodiscard]] constexpr bool is_external(location_kind kind) noexcept
{
    return kind == location_kind::url || kind == location_kind::path;
}

/// True for the blocks stored apart from the header, attached or external, whose place the location gives.
[[nodiscard]] constexpr bool is_stored_apart(location_kind kind) noexcept
{
    return kind == location_kind::attachment || is_external(kind);
}

/// What starts a path from the directory of the header file (spec §10.3).
inline constexpr std::string_view header_directory_prefix = "@header_dir/";

/// The value of a location attribute.
struct block_location
{
    location_kind kind = location_kind::embedded;
    /// For an inline block, the encoding of the character data.
    block_encoding encoding = block_encoding::base64;
    /// For an attachment, where the block starts in the file and its size, in bytes.
    std::uint64_t position = 0;
    std::uint64_t size = 0;
    /// For url and path, the URL or the file path as written between the parentheses. A path starts with a slash, or
    /// with @header_dir/ when it is relative to the directory of the header file.
    std::string reference{};
    /// For url and path, the identifier of the block in a data blocks file, in the forms with an index-id.
    std::optional<std::uint64_t> index_id{};

    friend bool operator==(const block_location&, const block_location&) = default;
};

/// Parses a location attribute. A URL or a path extends to the last closing parenthesis, so it can hold parentheses of
/// its own. Throws invalid_data_error with errc::invalid_location.
[[nodiscard]] block_location parse_location(std::string_view text);

/// Parses the encoding attribute of a Data element, or the encoding of an inline location: base64 or hex. Throws
/// invalid_data_error with errc::invalid_location.
[[nodiscard]] block_encoding parse_encoding(std::string_view text);

/// The byte order of the data of a block (spec §10.4).
enum class byte_order : std::uint8_t
{
    little,
    big,
};

/// Parses a byteOrder attribute: big or little. Throws invalid_data_error with errc::invalid_byte_order.
[[nodiscard]] byte_order parse_byte_order(std::string_view text);

/// The value of a checksum attribute (spec §10.5).
struct block_checksum
{
    hash_algorithm algorithm = hash_algorithm::sha1;
    std::vector<std::byte> digest{};

    friend bool operator==(const block_checksum&, const block_checksum&) = default;
};

/// Parses a checksum attribute, algorithm:digest. The algorithm is one of the names or alternate names of spec §10.5,
/// Table 9; the digest has its size in lowercase hexadecimal digits. Throws unsupported_error with
/// errc::unsupported_checksum for another algorithm name, and invalid_data_error with errc::invalid_checksum otherwise.
[[nodiscard]] block_checksum parse_checksum(std::string_view text);

/// True when name is an algorithm name or alternate name of spec §10.5, Table 9, as written there, in lowercase.
[[nodiscard]] bool is_checksum_algorithm_name(std::string_view name) noexcept;

/// True when name is a codec name of spec §10.6, with or without +sh, as written there, in lowercase.
[[nodiscard]] bool is_codec_name(std::string_view name) noexcept;

/// Parses a compression attribute, codec:uncompressed-size, or codec+sh:uncompressed-size:item-size for the codecs with
/// byte shuffling, whose item size is at least 1. subblocks stays empty. Throws unsupported_error with
/// errc::unsupported_compression for another codec name, and invalid_data_error with errc::invalid_compression
/// otherwise.
[[nodiscard]] block_compression parse_compression(std::string_view text);

/// Parses a subblocks attribute, c1,u1:c2,u2:...:cN,uN, with at least one subblock. Throws invalid_data_error with
/// errc::invalid_subblocks.
[[nodiscard]] std::vector<subblock> parse_subblocks(std::string_view text);

// The values that an encoder writes, which the parsers above read back.

/// The location of an attached block, attachment:position:size.
[[nodiscard]] std::string format_attachment(std::uint64_t position, std::uint64_t size);

/// An index-id in hexadecimal, as the specification recommends: 0x and 16 lowercase digits.
[[nodiscard]] std::string format_index_id(std::uint64_t id);

/// The location of a block in a data blocks file, by its path from the directory of the header file:
/// path(@header_dir/path):index-id.
[[nodiscard]] std::string format_relative_location(std::string_view path, std::uint64_t id);

/// The name of an algorithm in a checksum attribute: the alternate names sha1, sha256 and sha512, which PixInsight
/// writes, and sha3-256 and sha3-512, which have none.
[[nodiscard]] std::string_view checksum_name(hash_algorithm algorithm) noexcept;

/// The value of a checksum attribute, algorithm:digest, with the digest in lowercase hexadecimal digits.
[[nodiscard]] std::string format_checksum(const block_checksum& checksum);

/// The name of the codec of a compression attribute, with +sh for byte shuffling, such as zstd+sh.
[[nodiscard]] std::string_view compression_name(const block_compression& compression) noexcept;

/// The value of a compression attribute: codec:uncompressed-size, or codec+sh:uncompressed-size:item-size.
[[nodiscard]] std::string format_compression(const block_compression& compression);

/// The value of a subblocks attribute, c1,u1:c2,u2:...:cN,uN, for at least one subblock.
[[nodiscard]] std::string format_subblocks(const std::vector<subblock>& subblocks);

} // namespace openxisf::detail
