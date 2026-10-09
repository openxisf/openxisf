// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "support/fixture_builder.h"

#include <openxisf/io.h>

#include "support/bytes.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>

namespace openxisf::test {

namespace {

// The 16 bytes before the header of a monolithic file.
constexpr std::size_t monolithic_header_offset = 16;

template <typename T> void append_little_endian(std::vector<std::byte>& data, T value)
{
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        data.push_back(static_cast<std::byte>(value & 0xFFU));
        value >>= 8U;
    }
}

std::string with_attributes(std::string_view start, std::string_view attributes)
{
    std::string tag(start);
    if (!attributes.empty()) {
        tag += ' ';
        tag += attributes;
    }
    return tag;
}

std::string single_pixel(std::string_view name, std::string_view attributes, std::string_view content)
{
    return with_attributes("<" + std::string(name) +
                               R"( geometry="1:1:1" sampleFormat="UInt8" colorSpace="Gray")"
                               R"( location="embedded")",
                           attributes) +
           R"(><Data encoding="base64">AA==</Data>)" + std::string(content) + "</" + std::string(name) + ">";
}

} // namespace

std::string header_xml(std::string_view body, std::string_view metadata)
{
    std::string header(xml_declaration);
    header += root_start_tag;
    header += "<Metadata>";
    header += mandatory_metadata;
    header += metadata;
    header += "</Metadata>";
    header += body;
    header += "</xisf>";
    return header;
}

std::string image_xml(std::string_view attributes, std::string_view content)
{
    return single_pixel("Image", attributes, content);
}

std::string thumbnail_xml(std::string_view attributes, std::string_view content)
{
    return single_pixel("Thumbnail", attributes, content);
}

std::string keyword_xml(std::string_view attributes)
{
    return with_attributes(R"(<FITSKeyword name="OBSERVER" value="'Test'" comment="The observer")", attributes) + "/>";
}

std::vector<std::byte> monolithic_file(std::string_view header, const file_preamble& preamble)
{
    std::vector<std::byte> file = bytes(preamble.signature);
    append_little_endian(file, preamble.header_length.value_or(static_cast<std::uint32_t>(header.size())));
    append_little_endian(file, preamble.reserved);
    const std::vector<std::byte> text = bytes(header);
    file.insert(file.end(), text.begin(), text.end());
    return file;
}

std::vector<std::byte> file_with_attachments(std::string_view header, const std::vector<std::vector<std::byte>>& blocks)
{
    // The positions are written in decimal, so the header grows with them; the first block moves on until the header
    // fits before it.
    for (std::size_t start = 4096;; start *= 2) {
        std::string text(header);
        std::size_t position = start;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            const std::string placeholder = "{" + std::to_string(i) + "}";
            const std::string value = std::to_string(position) + ":" + std::to_string(blocks[i].size());
            for (std::size_t found = text.find(placeholder); found != std::string::npos;
                 found = text.find(placeholder, found + value.size())) {
                text.replace(found, placeholder.size(), value);
            }
            position += blocks[i].size();
        }
        if (monolithic_header_offset + text.size() <= start) {
            std::vector<std::byte> file = monolithic_file(text);
            file.resize(start);
            for (const std::vector<std::byte>& block : blocks) {
                file.insert(file.end(), block.begin(), block.end());
            }
            return file;
        }
    }
}

std::vector<placed_bytes> blocks_file_parts(const std::vector<index_node>& nodes,
                                            const std::vector<placed_bytes>& blocks, std::uint64_t reserved)
{
    std::vector<placed_bytes> parts{{.position = 0, .data = bytes("XISB0100")}};
    std::vector<std::byte> header;
    append_little_endian(header, reserved);
    parts.push_back({.position = 8, .data = header});
    for (const index_node& node : nodes) {
        std::vector<std::byte> data;
        append_little_endian(data, node.length.value_or(static_cast<std::uint32_t>(node.elements.size())));
        append_little_endian(data, node.reserved);
        append_little_endian(data, node.next);
        for (const index_entry& element : node.elements) {
            for (const std::uint64_t value :
                 {element.id, element.position, element.length, element.uncompressed_length, element.reserved}) {
                append_little_endian(data, value);
            }
        }
        parts.push_back({.position = node.position, .data = std::move(data)});
    }
    parts.insert(parts.end(), blocks.begin(), blocks.end());
    return parts;
}

std::vector<std::byte> blocks_file(const std::vector<index_node>& nodes, const std::vector<placed_bytes>& blocks,
                                   std::uint64_t size, std::uint64_t reserved)
{
    // std::size_t and std::uint64_t are the same type on some platforms, where a cast would be useless.
    const std::size_t initial = size;
    std::vector<std::byte> file(initial);
    for (const placed_bytes& part : blocks_file_parts(nodes, blocks, reserved)) {
        const std::size_t end = part.position + part.data.size();
        if (file.size() < end) {
            file.resize(end);
        }
        std::ranges::copy(part.data, file.begin() + static_cast<std::ptrdiff_t>(part.position));
    }
    return file;
}

std::vector<std::byte> blocks_file_of(const std::vector<std::vector<std::byte>>& blocks,
                                      const std::vector<std::uint64_t>& ids)
{
    index_node node;
    std::vector<placed_bytes> placed;
    std::uint64_t position = 16 + 16 + (40 * blocks.size());
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        node.elements.push_back({.id = ids.at(i), .position = position, .length = blocks[i].size()});
        placed.push_back({.position = position, .data = blocks[i]});
        position += blocks[i].size();
    }
    return blocks_file({node}, placed);
}

external_resolver memory_resolver(std::map<std::string, std::vector<std::byte>, std::less<>> files,
                                  std::shared_ptr<std::vector<std::string>> opened)
{
    auto shared = std::make_shared<const std::map<std::string, std::vector<std::byte>, std::less<>>>(std::move(files));
    return [shared, opened = std::move(opened)](const external_reference& reference) -> std::unique_ptr<input_source> {
        const auto found = shared->find(reference.location);
        if (found == shared->end()) {
            return nullptr;
        }
        if (opened) {
            opened->push_back(reference.location);
        }
        return std::make_unique<memory_source>(std::span<const std::byte>(found->second));
    };
}

reader open_unit(std::vector<std::byte> unit, read_options options)
{
    return reader(std::make_unique<memory_source>(std::move(unit)), std::move(options));
}

reader open_header(std::string_view header, read_options options)
{
    return open_unit(monolithic_file(header), std::move(options));
}

} // namespace openxisf::test
