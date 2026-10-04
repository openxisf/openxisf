// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <openxisf/error.h>
#include <openxisf/writer.h>

#include "codec/compressed_block.h"
#include "container/unit_writer.h"
#include "core/quote.h"
#include "core/utf8.h"
#include "core/uuid.h"
#include "core/xoshiro.h"
#include "io/paths.h"
#include "model/unit_contents.h"
#include "model/validation.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace openxisf {

namespace {

// Spec §9.6: monolithic files carry the .xisf suffix, header files .xish and data blocks files .xisb. File systems that
// ignore case take them in any case, so the writer does too.
bool has_suffix(std::string_view path, std::string_view suffix) noexcept
{
    if (path.size() < suffix.size()) {
        return false;
    }
    const std::string_view end = path.substr(path.size() - suffix.size());
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const char c = end[i] >= 'A' && end[i] <= 'Z' ? static_cast<char>(end[i] - 'A' + 'a') : end[i];
        if (c != suffix[i]) {
            return false;
        }
    }
    return true;
}

// The path of a data blocks file from the directory of the header file, as the header writes it in a location
// (spec §10.3): relative, in UNIX syntax, with a name at each step and nothing that XML or the XML schema of XISF
// cannot hold in an attribute.
void check_blocks_path(std::string_view path)
{
    std::string problem;
    if (!detail::is_valid_utf8(path)) {
        throw usage_error(errc::invalid_utf8, "the path of the data blocks file is not valid UTF-8");
    }
    if (!has_suffix(path, ".xisb")) {
        problem = "does not end with .xisb (spec §9.6)";
    } else if (path.starts_with('/')) {
        problem = "is absolute";
    } else if (std::ranges::any_of(
                   path, [](char c) { return c == '\\' || static_cast<unsigned char>(c) < 0x20 || c == 0x7F; })) {
        problem = "holds a backslash or a control character";
    } else {
        for (std::string_view rest = path; !rest.empty();) {
            const std::size_t slash = rest.find('/');
            const std::string_view step = rest.substr(0, slash);
            if (step.empty() || step == "." || step == "..") {
                problem = "has an empty step, or a . or .. step";
                break;
            }
            rest = slash == std::string_view::npos ? std::string_view() : rest.substr(slash + 1);
        }
    }
    if (!problem.empty()) {
        throw usage_error(errc::invalid_argument, "the path " + detail::quote(path) + " of the data blocks file " +
                                                      problem + ", which a header file cannot locate");
    }
}

// The uuid attribute of each image: its own, in lowercase, or a new one when the options ask for it.
std::vector<std::string> image_uuids(const std::vector<image_info>& images, bool generate)
{
    std::vector<std::string> uuids;
    uuids.reserve(images.size());
    std::optional<detail::xoshiro256starstar> generator;
    for (const image_info& image : images) {
        if (!image.uuid.empty()) {
            uuids.push_back(detail::format_uuid(detail::parse_uuid(image.uuid)));
        } else if (generate) {
            if (!generator) {
                generator = detail::xoshiro256starstar::from_random_device();
            }
            uuids.push_back(detail::format_uuid(detail::make_uuid_v4(*generator)));
        } else {
            uuids.emplace_back();
        }
    }
    return uuids;
}

date_time creation_time_of(const write_options& options)
{
    return options.creation_time.value_or(
        to_date_time(std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now())));
}

void write_valid_unit(const detail::unit_contents& unit, const write_options& options, output_sink& sink)
{
    const std::vector<std::string> uuids = image_uuids(unit.images, options.generate_uuids);
    detail::write_unit(unit, options, {.creation_time = creation_time_of(options), .uuids = uuids}, sink);
}

// The identifiers of the block index elements are random, as the specification recommends (spec §9.4).
void write_valid_distributed_unit(const detail::unit_contents& unit, const write_options& options, output_sink& header,
                                  output_sink& blocks, std::string_view blocks_path)
{
    const std::vector<std::string> uuids = image_uuids(unit.images, options.generate_uuids);
    detail::write_distributed_unit(unit, options, {.creation_time = creation_time_of(options), .uuids = uuids}, header,
                                   blocks, blocks_path, detail::xoshiro256starstar::from_random_device());
}

} // namespace

struct writer::state : detail::unit_contents
{
    write_options options;
};

writer::writer(write_options options) : state_(std::make_unique<state>())
{
    // The range of the level is that of every codec.
    (void)detail::codec_level(detail::compression_codec::zstd, options.compression_level);
    state_->options = std::move(options);
}

writer::writer(writer&& other) noexcept = default;
writer& writer::operator=(writer&& other) noexcept = default;
writer::~writer() = default;

const write_options& writer::options() const noexcept
{
    return state_->options;
}

property_list& writer::metadata() noexcept
{
    return state_->metadata;
}

const property_list& writer::metadata() const noexcept
{
    return state_->metadata;
}

property_list& writer::properties() noexcept
{
    return state_->properties;
}

const property_list& writer::properties() const noexcept
{
    return state_->properties;
}

std::vector<table>& writer::tables() noexcept
{
    return state_->tables;
}

const std::vector<table>& writer::tables() const noexcept
{
    return state_->tables;
}

std::size_t writer::add_image(image_info info, std::span<const std::byte> pixels)
{
    if (pixels.size() != info.data_size()) {
        throw usage_error(errc::invalid_argument, "the pixel data have " + std::to_string(pixels.size()) +
                                                      " bytes, and the image " + std::to_string(info.data_size()));
    }
    state_->images.push_back(std::move(info));
    state_->pixels.push_back(pixels);
    return state_->images.size() - 1;
}

std::span<const image_info> writer::images() const noexcept
{
    return state_->images;
}

void writer::check_sample_format(const image_info& info, sample_format format)
{
    if (info.sample_format != format) {
        throw usage_error(errc::invalid_argument, "the samples are of type " + std::string(sample_format_name(format)) +
                                                      ", and the image is " +
                                                      std::string(sample_format_name(info.sample_format)));
    }
}

void writer::save(std::string_view path) const
{
    if (!has_suffix(path, ".xisf")) {
        throw usage_error(errc::invalid_argument, "the path of a monolithic file must end with .xisf (spec §9.6)");
    }
    // Before the file exists, so that a unit that cannot be written leaves no trace.
    detail::validate_unit(*state_, state_->options);
    file_sink sink(path, {.flush_to_disk = state_->options.flush_to_disk});
    write_valid_unit(*state_, state_->options, sink);
}

void writer::save(output_sink& sink) const
{
    detail::validate_unit(*state_, state_->options);
    write_valid_unit(*state_, state_->options, sink);
}

void writer::save_distributed(std::string_view path) const
{
    if (!has_suffix(path, ".xish")) {
        throw usage_error(errc::invalid_argument, "the path of a header file must end with .xish (spec §9.6)");
    }
    const std::string blocks_path = std::string(path.substr(0, path.size() - 5)) + ".xisb";
    const std::string_view blocks_name = detail::file_name(blocks_path);
    check_blocks_path(blocks_name);
    // Before the files exist, so that a unit that cannot be written leaves no trace.
    detail::validate_unit(*state_, state_->options);
    file_sink blocks(blocks_path, {.flush_to_disk = state_->options.flush_to_disk});
    file_sink header(path, {.flush_to_disk = state_->options.flush_to_disk});
    write_valid_distributed_unit(*state_, state_->options, header, blocks, blocks_name);
}

void writer::save_distributed(output_sink& header, output_sink& blocks, std::string_view blocks_path) const
{
    if (&header == &blocks) {
        throw usage_error(errc::invalid_argument, "the header file and the data blocks file need two sinks");
    }
    check_blocks_path(blocks_path);
    detail::validate_unit(*state_, state_->options);
    write_valid_distributed_unit(*state_, state_->options, header, blocks, blocks_path);
}

} // namespace openxisf
