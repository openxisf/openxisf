// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "container/data_block.h"

#include "codec/compressed_block.h"
#include "core/checked_math.h"
#include "core/data_encoding.h"
#include "core/text_grammar.h"
#include "xml/xml_document.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace openxisf::detail {

namespace {

// The attributes that describe a block besides its location, with the code of their diagnostics.
struct block_attribute
{
    const char* name = nullptr;
    errc code = errc::invalid_location;
};

constexpr block_attribute byte_order_attribute{.name = "byteOrder", .code = errc::invalid_byte_order};
constexpr block_attribute checksum_attribute{.name = "checksum", .code = errc::invalid_checksum};
constexpr block_attribute compression_attribute{.name = "compression", .code = errc::invalid_compression};
constexpr block_attribute subblocks_attribute{.name = "subblocks", .code = errc::invalid_subblocks};
constexpr std::array<block_attribute, 4> block_attributes{byte_order_attribute, checksum_attribute,
                                                          compression_attribute, subblocks_attribute};

// The elements that can serialize a data block: images, thumbnails and ICC profiles, and the values of properties and
// table cells.
bool serializes_blocks(element_kind kind) noexcept
{
    return kind == element_kind::image || kind == element_kind::thumbnail || kind == element_kind::property ||
           kind == element_kind::icc_profile || kind == element_kind::cell;
}

// pugixml's iterators are not C++20 iterators, so the children are walked by hand.
bool has_child_elements(const pugi::xml_node& element)
{
    for (pugi::xml_node child = element.first_child(); !child.empty(); child = child.next_sibling()) {
        if (child.type() == pugi::node_element) {
            return true;
        }
    }
    return false;
}

// Turns the uppercase hexadecimal digits of text, from start on, into lowercase ones. True when there were any.
bool lowercase_hex_digits(std::string& text, std::size_t start) noexcept
{
    bool changed = false;
    for (std::size_t i = start; i < text.size(); ++i) {
        if (text[i] >= 'A' && text[i] <= 'F') {
            text[i] = static_cast<char>(text[i] - 'A' + 'a');
            changed = true;
        }
    }
    return changed;
}

// A block attribute as found, with where it was found.
struct attribute_value
{
    std::string_view text{};
    error_context context{};
};

// Describes the block of one element (spec §10). Tolerated problems are warnings; the first problem that leaves the
// block without a meaning is an error, which makes it unavailable, and ends the description.
class element_block
{
public:
    element_block(const unit_outline& outline, std::size_t index, const block_context& context, diagnostic_log& log)
        : outline_(outline), element_(outline.elements[index]), context_(context), log_(log), block_{.element = index}
    {}

    // The block, or nothing when the element has no location attribute.
    std::optional<data_block> describe()
    {
        const pugi::xml_attribute location = element_.node.attribute("location");
        if (!location) {
            ignore_block_parts();
            return std::nullopt;
        }
        block_.path = outline_.path(block_.element);
        if (locate(location.value()) && find_data_element() && read_attributes() && read_contents() &&
            check_subblocks() && verify()) {
            block_.descriptor = std::move(descriptor_);
        }
        return std::move(block_);
    }

private:
    // Records an error that makes the block unavailable, and returns false. In strict mode, the log throws instead.
    bool fail(errc code, std::string message, error_context context)
    {
        log_.error(code, std::move(message), std::move(context));
        block_.problem = log_.entries().back();
        return false;
    }

    // The result of parse, or nothing when it threw the error of a malformed or unsupported value, which is recorded.
    template <typename Parse>
    std::optional<std::invoke_result_t<Parse>> attempt(Parse&& parse, const error_context& context)
    {
        try {
            return std::forward<Parse>(parse)();
        } catch (const invalid_data_error& failure) {
            fail(failure.code(), failure.what(), context);
        } catch (const unsupported_error& failure) {
            fail(failure.code(), failure.what(), context);
        }
        return std::nullopt;
    }

    // The Data elements that the element contains.
    std::vector<std::size_t> data_elements() const
    {
        std::vector<std::size_t> found;
        for (std::size_t i = block_.element + 1; i < element_.end; ++i) {
            if (outline_.elements[i].parent == block_.element && outline_.elements[i].kind == element_kind::data) {
                found.push_back(i);
            }
        }
        return found;
    }

    void ignore_data_elements(std::string_view reason)
    {
        for (const std::size_t data : data_elements()) {
            log_.warning(errc::invalid_location, "the Data element is ignored, since " + std::string(reason),
                         {.element = outline_.path(data)});
        }
    }

    // Without a location, the element has no block, and what would describe one means nothing.
    void ignore_block_parts()
    {
        ignore_data_elements("the element has no data block");
        for (const block_attribute& attribute : block_attributes) {
            if (!element_.node.attribute(attribute.name).empty()) {
                log_.warning(attribute.code,
                             "the attribute " + std::string(attribute.name) +
                                 " is ignored, since the element has no data block",
                             {.element = outline_.path(block_.element), .attribute = attribute.name});
            }
        }
    }

    // The location, and the rules of spec §10.1 and §10.2 about where blocks of each kind can be.
    bool locate(std::string_view text)
    {
        const error_context context{.element = block_.path, .attribute = "location"};
        const std::optional<block_location> location = attempt([text] { return parse_location(text); }, context);
        if (!location) {
            return false;
        }
        const bool external = is_external(location->kind);
        if (location->kind == location_kind::attachment && context_.storage == unit_storage::distributed) {
            return fail(errc::invalid_location, "a header file cannot have attached data blocks", context);
        }
        if (external && context_.storage == unit_storage::monolithic) {
            return fail(errc::invalid_location, "a monolithic file cannot have external data blocks", context);
        }
        descriptor_.location = *location;
        return true;
    }

    // An embedded block is in its only Data element (spec §10.3). The Data elements of other blocks are ignored.
    bool find_data_element()
    {
        if (descriptor_.location.kind != location_kind::embedded) {
            ignore_data_elements("the data block is not embedded");
            return true;
        }
        const std::vector<std::size_t> found = data_elements();
        if (found.size() != 1) {
            return fail(
                errc::invalid_location,
                "the data block is embedded, but the element has " +
                    (found.empty() ? std::string("no Data element") : std::to_string(found.size()) + " Data elements"),
                {.element = block_.path, .attribute = "location"});
        }
        data_ = found.front();
        data_path_ = outline_.path(data_);
        return true;
    }

    // A block attribute: on the Data element of an embedded block, where it belongs, or on the element itself. On the
    // element, an attribute of an embedded block is misplaced but unambiguous, so it is used, with a warning, unless
    // the Data element has it too.
    std::optional<attribute_value> attribute(const block_attribute& which)
    {
        const pugi::xml_attribute own = element_.node.attribute(which.name);
        if (data_ == no_element) {
            if (!own) {
                return std::nullopt;
            }
            return attribute_value{.text = own.value(), .context = {.element = block_.path, .attribute = which.name}};
        }
        const pugi::xml_attribute inner = outline_.elements[data_].node.attribute(which.name);
        if (!own.empty()) {
            log_.warning(which.code,
                         "the attribute " + std::string(which.name) +
                             " of an embedded block belongs to its Data element" +
                             (inner.empty() ? "" : ", and is ignored"),
                         {.element = block_.path, .attribute = which.name});
            if (!inner) {
                return attribute_value{.text = own.value(),
                                       .context = {.element = block_.path, .attribute = which.name}};
            }
        }
        if (!inner) {
            return std::nullopt;
        }
        return attribute_value{.text = inner.value(), .context = {.element = data_path_, .attribute = which.name}};
    }

    bool read_attributes()
    {
        if (const std::optional<attribute_value> value = attribute(byte_order_attribute)) {
            // ICC profiles are big-endian structures, kept unaltered (spec §10.4, §11.7).
            if (element_.kind == element_kind::icc_profile) {
                log_.warning(errc::invalid_byte_order, "an ICCProfile element cannot have a byteOrder attribute",
                             value->context);
            } else {
                const std::optional<byte_order> order =
                    attempt([&value] { return parse_byte_order(value->text); }, value->context);
                if (!order) {
                    return false;
                }
                descriptor_.order = *order;
            }
        }
        if (const std::optional<attribute_value> value = attribute(checksum_attribute)) {
            checksum_context_ = value->context;
            descriptor_.checksum = read_checksum(*value);
            if (!descriptor_.checksum) {
                return false;
            }
        }
        if (const std::optional<attribute_value> value = attribute(compression_attribute)) {
            compression_context_ = value->context;
            descriptor_.compression = attempt([&value] { return parse_compression(value->text); }, value->context);
            if (!descriptor_.compression) {
                return false;
            }
        }
        if (const std::optional<attribute_value> value = attribute(subblocks_attribute)) {
            subblocks_context_ = value->context;
            if (!descriptor_.compression) {
                log_.warning(errc::invalid_subblocks,
                             "the attribute subblocks is ignored, since the data block is not compressed",
                             value->context);
                return true;
            }
            std::optional<std::vector<subblock>> subblocks =
                attempt([&value] { return parse_subblocks(value->text); }, value->context);
            if (!subblocks) {
                return false;
            }
            descriptor_.compression->subblocks = std::move(*subblocks);
        }
        return true;
    }

    // Uppercase hexadecimal digits name the same digest, so they are accepted with a warning.
    std::optional<block_checksum> read_checksum(const attribute_value& value)
    {
        std::string text(value.text);
        const std::size_t colon = text.find(':');
        if (colon != std::string::npos && lowercase_hex_digits(text, colon + 1)) {
            log_.warning(errc::invalid_checksum, "the digest has uppercase hexadecimal digits", value.context);
        }
        return attempt([&text] { return parse_checksum(text); }, value.context);
    }

    // The stored bytes of an inline or embedded block, and the place of an attached one.
    bool read_contents()
    {
        switch (descriptor_.location.kind) {
        case location_kind::attachment:
            return check_bounds();
        case location_kind::inline_data:
            // Spec §11.5, §11.12: an image cannot be inline, because its element can have child elements. Spec §10.3:
            // the element of an inline block has no child elements. Its character data are still the block.
            if (element_.kind == element_kind::image || element_.kind == element_kind::thumbnail) {
                log_.warning(errc::invalid_location,
                             "an " + std::string(element_name(element_.kind)) +
                                 " element cannot have an inline data block",
                             {.element = block_.path, .attribute = "location"});
            } else if (has_child_elements(element_.node)) {
                log_.warning(errc::invalid_location, "an element with an inline data block cannot have child elements",
                             {.element = block_.path, .attribute = "location"});
            }
            return decode(character_data(element_.node), descriptor_.location.encoding, {.element = block_.path});
        case location_kind::embedded:
            return decode_embedded();
        case location_kind::url:
        case location_kind::path:
            // Its file and its place in it are found once every block is described (locate_external_blocks()).
            break;
        }
        return true;
    }

    bool check_bounds()
    {
        const block_location& location = descriptor_.location;
        const error_context context{.element = block_.path, .attribute = "location", .offset = location.position};
        if (location.position < context_.header_end) {
            return fail(errc::block_out_of_bounds,
                        "the attached data block starts at byte " + std::to_string(location.position) +
                            ", inside the header, which ends at byte " + std::to_string(context_.header_end),
                        context);
        }
        if (location.position > context_.source_size || location.size > context_.source_size - location.position) {
            return fail(errc::block_out_of_bounds,
                        "the attached data block of " + std::to_string(location.size) + " bytes at byte " +
                            std::to_string(location.position) + " goes past the end of the file, which has " +
                            std::to_string(context_.source_size) + " bytes",
                        context);
        }
        if (location.size == 0) {
            warn_empty();
        }
        return true;
    }

    bool decode_embedded()
    {
        const pugi::xml_node data = outline_.elements[data_].node;
        const pugi::xml_attribute encoding = data.attribute("encoding");
        if (!encoding) {
            return fail(errc::invalid_location, "the Data element of an embedded block has no encoding attribute",
                        {.element = data_path_});
        }
        const std::optional<block_encoding> parsed = attempt([&encoding] { return parse_encoding(encoding.value()); },
                                                             {.element = data_path_, .attribute = "encoding"});
        if (!parsed || !decode(character_data(data), *parsed, {.element = data_path_})) {
            return false;
        }
        if (descriptor_.data.empty()) {
            warn_empty();
        }
        return true;
    }

    // Base64 data without padding, and uppercase hexadecimal digits, are unambiguous, so they are accepted with a
    // warning, and decoded as if they were written as the specification requires.
    bool decode(std::string text, block_encoding encoding, const error_context& context)
    {
        if (encoding == block_encoding::base64) {
            const auto digits =
                static_cast<std::size_t>(std::ranges::count_if(text, [](char c) { return !is_white_space(c); }));
            if (text.find('=') == std::string::npos && digits % 4 >= 2) {
                log_.warning(errc::invalid_base64, "the Base64 data have no padding", context);
                text.append(4 - (digits % 4), '=');
            }
        } else if (lowercase_hex_digits(text, 0)) {
            log_.warning(errc::invalid_hex, "the hexadecimal data have uppercase digits", context);
        }

        std::optional<std::vector<std::byte>> bytes = attempt(
            [&text, encoding] { return encoding == block_encoding::base64 ? decode_base64(text) : decode_hex(text); },
            context);
        if (!bytes) {
            return false;
        }
        descriptor_.data = std::move(*bytes);
        return true;
    }

    // Spec §10: only an inline block, the value of an empty vector or matrix, can have no bytes.
    void warn_empty()
    {
        log_.warning(errc::invalid_location, "the data block is empty, which only an inline block can be",
                     {.element = block_.path, .attribute = "location"});
    }

    // The subblocks must account for every byte of the block, stored and uncompressed, and each must fit the decoder of
    // its codec (spec §10.6). A block without subblocks is one subblock.
    bool check_subblocks()
    {
        // The stored size of an external block is known once it is located.
        if (!descriptor_.compression || is_external(descriptor_.location.kind)) {
            return true;
        }
        const block_compression& compression = *descriptor_.compression;
        const std::uint64_t stored = descriptor_.location.kind == location_kind::attachment ? descriptor_.location.size
                                                                                            : descriptor_.data.size();
        const error_context& context = compression.subblocks.empty() ? compression_context_ : subblocks_context_;
        return attempt([&compression, stored] { return subblocks_of(compression, stored); }, context).has_value();
    }

    // The bytes of an inline or embedded block are at hand, so they are verified now (spec §10.5); an attached or
    // external block is verified when it is read.
    bool verify()
    {
        if (!descriptor_.checksum || is_stored_apart(descriptor_.location.kind)) {
            return true;
        }
        const block_checksum& checksum = *descriptor_.checksum;
        if (compute_digest(checksum.algorithm, descriptor_.data) != checksum.digest) {
            return fail(errc::checksum_mismatch,
                        "the " + std::string(hash_name(checksum.algorithm)) +
                            " digest of the data block differs from its checksum",
                        checksum_context_);
        }
        return true;
    }

    const unit_outline& outline_;
    const outline_element& element_;
    const block_context& context_;
    diagnostic_log& log_;
    data_block block_;
    block_descriptor descriptor_{};
    // The Data element of an embedded block, and its path.
    std::size_t data_ = no_element;
    std::string data_path_{};
    // Where the checksum, compression and subblocks attributes were found, for later diagnostics about them.
    error_context checksum_context_{};
    error_context compression_context_{};
    error_context subblocks_context_{};
};

// The stored bytes of an available block, verified, read in pieces when there is a progress function. The errors have
// no context.
std::vector<std::byte> read_stored_bytes(const thread_safe_source& source, const block_descriptor& descriptor,
                                         const limits& limits, const block_progress& progress, std::size_t piece_size)
{
    if (!is_stored_apart(descriptor.location.kind)) {
        if (progress) {
            progress(descriptor.data.size());
        }
        return descriptor.data;
    }
    // An external block is in its own file.
    const bool external = is_external(descriptor.location.kind);
    if (external && !descriptor.external) {
        throw unsupported_error(errc::unsupported_location, "the external data block has not been located");
    }
    const thread_safe_source& file = external ? *descriptor.external : source;

    const std::uint64_t size = descriptor.location.size;
    if (limits.max_allocation != 0 && size > limits.max_allocation) {
        throw limit_error(errc::allocation_too_large, "the data block has " + std::to_string(size) +
                                                          " bytes, more than the allocation limit of " +
                                                          std::to_string(limits.max_allocation));
    }
    std::vector<std::byte> data(checked_cast<std::size_t>(size));
    if (!progress) {
        file.read(descriptor.location.position, data);
    } else {
        for (std::size_t done = 0; done < data.size();) {
            const std::size_t piece = std::min(std::max<std::size_t>(piece_size, 1), data.size() - done);
            file.read(descriptor.location.position + done, std::span(data).subspan(done, piece));
            done += piece;
            progress(done);
        }
    }
    // Spec §10.5 and §10.6.1: nothing is returned, and so nothing is decompressed, before the digest matches.
    if (descriptor.checksum && compute_digest(descriptor.checksum->algorithm, data) != descriptor.checksum->digest) {
        throw integrity_error(errc::checksum_mismatch, "the " + std::string(hash_name(descriptor.checksum->algorithm)) +
                                                           " digest of the data block differs from its checksum");
    }
    return data;
}

// The result of read, whose errors have no context, with the errors given the context of block: its element, and the
// position of an attachment. What the source throws passes through.
template <typename Read> std::vector<std::byte> with_context_of(const data_block& block, Read&& read)
{
    error_context context{.element = block.path};
    if (block.descriptor && block.descriptor->location.kind == location_kind::attachment) {
        context.offset = block.descriptor->location.position;
    }
    try {
        return std::forward<Read>(read)();
    } catch (const integrity_error& failure) {
        throw integrity_error(failure.code(), failure.what(), std::move(context));
    } catch (const limit_error& failure) {
        throw limit_error(failure.code(), failure.what(), std::move(context));
    } catch (const unsupported_error& failure) {
        throw unsupported_error(failure.code(), failure.what(), std::move(context));
    } catch (const invalid_data_error& failure) {
        throw invalid_data_error(failure.code(), failure.what(), std::move(context));
    }
}

[[noreturn]] void throw_unavailable(const data_block& block)
{
    if (block.problem) {
        throw_unit_error(block.problem->code, block.problem->message, block.problem->context);
    }
    throw usage_error(errc::invalid_argument, "the data block has neither a descriptor nor a problem",
                      {.element = block.path});
}

} // namespace

std::vector<data_block> describe_blocks(const unit_outline& outline, const block_context& context, diagnostic_log& log)
{
    std::vector<data_block> blocks;
    for (std::size_t index = 0; index < outline.elements.size(); ++index) {
        if (serializes_blocks(outline.elements[index].kind)) {
            if (std::optional<data_block> block = element_block(outline, index, context, log).describe()) {
                blocks.push_back(std::move(*block));
            }
        }
    }
    return blocks;
}

std::vector<std::byte> read_stored_block(const thread_safe_source& source, const data_block& block,
                                         const limits& limits)
{
    if (!block.descriptor) {
        throw_unavailable(block);
    }
    const block_descriptor& descriptor = *block.descriptor;
    return with_context_of(block, [&] { return read_stored_bytes(source, descriptor, limits, {}, 0); });
}

std::vector<std::byte> read_block(const thread_safe_source& source, const data_block& block, const limits& limits,
                                  const block_progress& progress, std::size_t piece_size)
{
    if (!block.descriptor) {
        throw_unavailable(block);
    }
    const block_descriptor& descriptor = *block.descriptor;
    return with_context_of(block, [&] { return read_block_data(source, descriptor, limits, progress, piece_size); });
}

std::vector<std::byte> read_block_data(const thread_safe_source& source, const block_descriptor& descriptor,
                                       const limits& limits, const block_progress& progress, std::size_t piece_size)
{
    std::vector<std::byte> stored = read_stored_bytes(source, descriptor, limits, progress, piece_size);
    if (!descriptor.compression) {
        return stored;
    }
    subblock_progress decompressed;
    if (progress) {
        decompressed = [&progress, read = stored.size()](std::uint64_t done) { progress(read + done); };
    }
    return decompress_block(stored, *descriptor.compression, limits, decompressed);
}

std::uint64_t stored_size(const block_descriptor& descriptor) noexcept
{
    return is_stored_apart(descriptor.location.kind) ? descriptor.location.size : descriptor.data.size();
}

std::uint64_t data_size(const block_descriptor& descriptor) noexcept
{
    return descriptor.compression ? descriptor.compression->uncompressed_size : stored_size(descriptor);
}

std::uint64_t work_size(const block_descriptor& descriptor) noexcept
{
    // The stored size is within the source, but a hostile uncompressed size can take the sum beyond 64 bits; it then
    // saturates, since a read of such a block fails before its progress matters.
    const std::uint64_t stored = stored_size(descriptor);
    if (!descriptor.compression) {
        return stored;
    }
    const std::uint64_t uncompressed = descriptor.compression->uncompressed_size;
    return uncompressed > std::numeric_limits<std::uint64_t>::max() - stored ? std::numeric_limits<std::uint64_t>::max()
                                                                             : stored + uncompressed;
}

} // namespace openxisf::detail
