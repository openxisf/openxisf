// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/property.h>
#include <openxisf/writer.h>

#include "container/block_attributes.h"
#include "model/unit_contents.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The XML header that the writer writes (spec §9.5, §11): the elements of a unit, built once from its model, then
// written as text whenever the places of its data blocks change.

namespace openxisf::detail {

/// An element of a header, with its attributes and character data as plain text, escaped when the header is written.
struct xml_element
{
    std::string name{};
    std::vector<std::pair<std::string, std::string>> attributes{};
    std::string text{};
    std::vector<xml_element> children{};
    /// The data block that the element serializes, as an index into header_tree::blocks; its attributes follow those
    /// of the element.
    std::optional<std::size_t> block{};
};

/// The data of a block to write, in little-endian byte order (spec §10.4).
struct block_source
{
    /// The data, when they are borrowed: the pixel data of the caller, or of a thumbnail of the model.
    std::optional<std::span<const std::byte>> borrowed{};
    /// The data, when the builder made them.
    std::vector<std::byte> owned{};
    /// The size of the numbers that the block holds, the item size of byte shuffling (spec §10.6.2); 1 for bytes.
    std::size_t item_size = 1;
    /// True for a block written in the header, as Base64, and false for an attached one.
    bool inline_data = false;

    [[nodiscard]] std::span<const std::byte> data() const noexcept
    {
        return borrowed ? *borrowed : std::span<const std::byte>(owned);
    }
};

/// The header of a unit, but for the metadata that the writer generates and the attributes of the data blocks.
struct header_tree
{
    /// The Property elements of the Metadata element that the model holds.
    std::vector<xml_element> metadata{};
    /// The other children of the root element, in order: the Image elements, then the standalone Property and Table
    /// elements.
    std::vector<xml_element> body{};
    /// The data blocks, in document order. A block of a Property, Cell or ICCProfile element is inline when it has at
    /// most options.max_inline_block_size bytes; every other block is attached.
    std::vector<block_source> blocks{};
};

/// Builds the header of a unit that validate_unit() accepted. uuids holds the uuid attribute of each image: its own,
/// in lowercase, a generated one, or nothing.
[[nodiscard]] header_tree build_header_tree(const unit_contents& unit, const write_options& options,
                                            std::span<const std::string> uuids);

/// The Property elements of properties whose values need no data block, such as the generated metadata.
[[nodiscard]] std::vector<xml_element> property_elements(std::span<const property> properties);

/// What the header says about a data block: where it is, and how it is encoded (spec §10.3 to §10.6).
struct block_header
{
    bool inline_data = false;
    /// The stored bytes in Base64, for an inline block.
    std::string text{};
    /// The place of an attached block.
    std::uint64_t position = 0;
    std::uint64_t size = 0;
    /// How the block is compressed, when it is. Its subblocks are written when it lists them.
    std::optional<block_compression> compression{};
    std::optional<block_checksum> checksum{};
};

/// The text of the header: the XML declaration, the initial comment, and the root element, whose Metadata element
/// holds generated and then the metadata of tree. Each data block is described by blocks[i].
[[nodiscard]] std::string format_header(const header_tree& tree, std::span<const xml_element> generated,
                                        std::span<const block_header> blocks);

} // namespace openxisf::detail
