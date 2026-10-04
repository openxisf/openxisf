// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "container/block_attributes.h"

#include <openxisf/error.h>

#include "core/data_encoding.h"
#include "core/quote.h"
#include "core/text_grammar.h"

#include <algorithm>
#include <array>
#include <optional>

namespace openxisf::detail {

namespace {

struct algorithm_name
{
    std::string_view name{};
    hash_algorithm algorithm{};
};

// Spec §10.5, Table 9: the names and the alternate names.
constexpr std::array<algorithm_name, 8> algorithm_names{{
    {.name = "sha-1", .algorithm = hash_algorithm::sha1},
    {.name = "sha1", .algorithm = hash_algorithm::sha1},
    {.name = "sha-256", .algorithm = hash_algorithm::sha256},
    {.name = "sha256", .algorithm = hash_algorithm::sha256},
    {.name = "sha-512", .algorithm = hash_algorithm::sha512},
    {.name = "sha512", .algorithm = hash_algorithm::sha512},
    {.name = "sha3-256", .algorithm = hash_algorithm::sha3_256},
    {.name = "sha3-512", .algorithm = hash_algorithm::sha3_512},
}};

struct codec_entry
{
    std::string_view name{};
    compression_codec codec{};
    bool shuffled = false;
};

// Spec §10.6.3 to §10.6.10.
constexpr std::array<codec_entry, 8> codec_names{{
    {.name = "zlib", .codec = compression_codec::zlib},
    {.name = "zlib+sh", .codec = compression_codec::zlib, .shuffled = true},
    {.name = "lz4", .codec = compression_codec::lz4},
    {.name = "lz4+sh", .codec = compression_codec::lz4, .shuffled = true},
    {.name = "lz4hc", .codec = compression_codec::lz4hc},
    {.name = "lz4hc+sh", .codec = compression_codec::lz4hc, .shuffled = true},
    {.name = "zstd", .codec = compression_codec::zstd},
    {.name = "zstd+sh", .codec = compression_codec::zstd, .shuffled = true},
}};

constexpr std::string_view header_directory = "@header_dir/";

// The algorithm of a name of Table 9 of spec §10.5, compared exactly.
std::optional<hash_algorithm> algorithm_named(std::string_view name) noexcept
{
    for (const algorithm_name& entry : algorithm_names) {
        if (entry.name == name) {
            return entry.algorithm;
        }
    }
    return std::nullopt;
}

// The codec of a name of spec §10.6, compared exactly.
std::optional<codec_entry> codec_named(std::string_view name) noexcept
{
    for (const codec_entry& entry : codec_names) {
        if (entry.name == name) {
            return entry;
        }
    }
    return std::nullopt;
}

// The parts of text between separators: one more than there are separators.
std::vector<std::string_view> split(std::string_view text, char separator)
{
    std::vector<std::string_view> parts;
    for (;;) {
        const std::size_t end = text.find(separator);
        parts.push_back(text.substr(0, end));
        if (end == std::string_view::npos) {
            return parts;
        }
        text.remove_prefix(end + 1);
    }
}

// An unsigned integer of spec §8.3.1 or §8.3.2 that is a part of an attribute value. A failure says which part.
std::uint64_t parse_part(std::string_view part, errc code, std::string_view attribute, std::string_view value,
                         std::string_view meaning)
{
    try {
        return parse_integer<std::uint64_t>(part);
    } catch (const invalid_data_error& failure) {
        throw invalid_data_error(code, "the " + std::string(attribute) + " " + quote(value) + " has an invalid " +
                                           std::string(meaning) + ": " + failure.what());
    }
}

[[noreturn]] void throw_invalid_location(std::string_view text, std::string_view reason)
{
    throw invalid_data_error(errc::invalid_location, "the location " + quote(text) + " " + std::string(reason));
}

// url(...) and path(...), each optionally followed by :index-id. The reference extends to the last closing
// parenthesis (spec §10.3).
block_location parse_external(std::string_view text, location_kind kind, std::string_view opening)
{
    const std::size_t closing = text.rfind(')');
    if (closing == std::string_view::npos) {
        throw_invalid_location(text, "has no closing parenthesis");
    }
    block_location location{.kind = kind,
                            .reference = std::string(text.substr(opening.size(), closing - opening.size()))};
    if (location.reference.empty()) {
        throw_invalid_location(text, "has nothing between its parentheses");
    }
    const std::string_view rest = text.substr(closing + 1);
    if (!rest.empty()) {
        if (rest.front() != ':') {
            throw_invalid_location(text, "has something other than an index-id after its closing parenthesis");
        }
        location.index_id = parse_part(rest.substr(1), errc::invalid_location, "location", text, "index-id");
    }
    return location;
}

// A path is absolute, or relative to the directory of the header file, and names something (spec §10.3).
void check_path(std::string_view text, std::string_view path)
{
    const bool absolute = path.size() > 1 && path.front() == '/';
    const bool relative = path.size() > header_directory.size() && path.starts_with(header_directory);
    if (!absolute && !relative) {
        throw_invalid_location(text, "holds neither an absolute path nor a path that starts with @header_dir/");
    }
}

} // namespace

block_location parse_location(std::string_view text)
{
    if (text == "embedded") {
        return {.kind = location_kind::embedded};
    }
    if (constexpr std::string_view prefix = "inline:"; text.starts_with(prefix)) {
        return {.kind = location_kind::inline_data, .encoding = parse_encoding(text.substr(prefix.size()))};
    }
    if (constexpr std::string_view prefix = "attachment:"; text.starts_with(prefix)) {
        const std::vector<std::string_view> parts = split(text.substr(prefix.size()), ':');
        if (parts.size() != 2) {
            throw_invalid_location(text, "does not have the form attachment:position:size");
        }
        return {.kind = location_kind::attachment,
                .position = parse_part(parts[0], errc::invalid_location, "location", text, "position"),
                .size = parse_part(parts[1], errc::invalid_location, "location", text, "size")};
    }
    if (constexpr std::string_view prefix = "url("; text.starts_with(prefix)) {
        return parse_external(text, location_kind::url, prefix);
    }
    if (constexpr std::string_view prefix = "path("; text.starts_with(prefix)) {
        block_location location = parse_external(text, location_kind::path, prefix);
        check_path(text, location.reference);
        return location;
    }
    throw_invalid_location(text, "is not inline, embedded, attachment, url() or path()");
}

block_encoding parse_encoding(std::string_view text)
{
    if (text == "base64") {
        return block_encoding::base64;
    }
    if (text == "hex") {
        return block_encoding::hex;
    }
    throw invalid_data_error(errc::invalid_location,
                             "the encoding " + quote(text) + " of a data block is neither base64 nor hex");
}

byte_order parse_byte_order(std::string_view text)
{
    if (text == "little") {
        return byte_order::little;
    }
    if (text == "big") {
        return byte_order::big;
    }
    throw invalid_data_error(errc::invalid_byte_order, "the byte order " + quote(text) + " is neither big nor little");
}

block_checksum parse_checksum(std::string_view text)
{
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos || colon == 0) {
        throw invalid_data_error(errc::invalid_checksum,
                                 "the checksum " + quote(text) + " does not have the form algorithm:digest");
    }
    const std::string_view name = text.substr(0, colon);
    const std::optional<hash_algorithm> algorithm = algorithm_named(name);
    if (!algorithm) {
        throw unsupported_error(errc::unsupported_checksum,
                                "the checksum algorithm " + quote(name) + " is not supported");
    }

    // decode_hex() would skip white space, which has no place in a digest.
    const std::string_view digest = text.substr(colon + 1);
    const std::size_t size = digest_size(*algorithm);
    const auto is_hex_digit = [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); };
    if (digest.size() != 2 * size || !std::ranges::all_of(digest, is_hex_digit)) {
        throw invalid_data_error(errc::invalid_checksum, "the digest of the checksum " + quote(text) + " is not " +
                                                             std::to_string(2 * size) +
                                                             " lowercase hexadecimal digits");
    }
    return {.algorithm = *algorithm, .digest = decode_hex(digest)};
}

block_compression parse_compression(std::string_view text)
{
    const std::vector<std::string_view> parts = split(text, ':');
    const std::optional<codec_entry> found = codec_named(parts.front());
    if (!found) {
        if (parts.front().empty()) {
            throw invalid_data_error(errc::invalid_compression,
                                     "the compression " + quote(text) + " does not start with a codec name");
        }
        throw unsupported_error(errc::unsupported_compression,
                                "the compression codec " + quote(parts.front()) + " is not supported");
    }
    if (parts.size() != (found->shuffled ? 3U : 2U)) {
        throw invalid_data_error(errc::invalid_compression,
                                 "the compression " + quote(text) + " does not have the form " +
                                     std::string(found->name) +
                                     (found->shuffled ? ":uncompressed-size:item-size" : ":uncompressed-size"));
    }
    block_compression compression{
        .codec = found->codec,
        .uncompressed_size = parse_part(parts[1], errc::invalid_compression, "compression", text, "uncompressed size")};
    if (found->shuffled) {
        compression.item_size = parse_part(parts[2], errc::invalid_compression, "compression", text, "item size");
        if (compression.item_size == 0) {
            throw invalid_data_error(errc::invalid_compression,
                                     "the compression " + quote(text) + " has an item size of zero");
        }
    }
    return compression;
}

std::vector<subblock> parse_subblocks(std::string_view text)
{
    std::vector<subblock> subblocks;
    for (const std::string_view pair : split(text, ':')) {
        const std::vector<std::string_view> sizes = split(pair, ',');
        if (sizes.size() != 2) {
            throw invalid_data_error(errc::invalid_subblocks, "the subblocks " + quote(text) +
                                                                  " are not compressed,uncompressed pairs separated "
                                                                  "by colons");
        }
        subblocks.push_back(
            {.compressed_size = parse_part(sizes[0], errc::invalid_subblocks, "subblocks", text, "compressed size"),
             .uncompressed_size =
                 parse_part(sizes[1], errc::invalid_subblocks, "subblocks", text, "uncompressed size")});
    }
    return subblocks;
}

} // namespace openxisf::detail
