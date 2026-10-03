// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "model/image_attributes.h"

#include <openxisf/error.h>

#include "core/quote.h"
#include "core/text_grammar.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

namespace openxisf::detail {

namespace {

constexpr std::array sample_formats{sample_format::uint8,     sample_format::uint16,   sample_format::uint32,
                                    sample_format::uint64,    sample_format::float32,  sample_format::float64,
                                    sample_format::complex32, sample_format::complex64};

constexpr std::array color_spaces{color_space::gray, color_space::rgb, color_space::cie_lab};

constexpr std::array storages{pixel_storage::planar, pixel_storage::normal};

constexpr std::array image_types{image_type::bias,
                                 image_type::dark,
                                 image_type::flat,
                                 image_type::light,
                                 image_type::master_bias,
                                 image_type::master_dark,
                                 image_type::master_flat,
                                 image_type::master_light,
                                 image_type::defect_map,
                                 image_type::rejection_map_high,
                                 image_type::rejection_map_low,
                                 image_type::binary_rejection_map_high,
                                 image_type::binary_rejection_map_low,
                                 image_type::slope_map,
                                 image_type::weight_map};

constexpr std::array orientations{orientation::none,
                                  orientation::flip,
                                  orientation::rotate_90,
                                  orientation::rotate_90_flip,
                                  orientation::rotate_minus_90,
                                  orientation::rotate_minus_90_flip,
                                  orientation::rotate_180,
                                  orientation::rotate_180_flip};

// The value of values whose name is text, or nothing.
template <typename Value, std::size_t Size>
std::optional<Value> named(std::string_view text, const std::array<Value, Size>& values,
                           std::string_view (*name)(Value) noexcept) noexcept
{
    for (const Value value : values) {
        if (name(value) == text) {
            return value;
        }
    }
    return std::nullopt;
}

// A floating point value of spec §8.3.3 that is finite, or nothing.
std::optional<double> finite_float(std::string_view text)
{
    try {
        const auto value = parse_float<double>(text);
        return std::isfinite(value) ? std::optional(value) : std::nullopt;
    } catch (const invalid_data_error&) {
        return std::nullopt;
    }
}

[[noreturn]] void throw_invalid_geometry(std::string_view text, std::string_view problem)
{
    throw invalid_data_error(errc::invalid_geometry, "the geometry " + quote(text) + std::string(problem));
}

} // namespace

geometry parse_geometry(std::string_view text)
{
    geometry result;
    std::uint64_t samples = 1;
    std::size_t start = 0;
    while (true) {
        const std::size_t colon = text.find(':', start);
        const std::string_view item = text.substr(start, colon == std::string_view::npos ? colon : colon - start);
        std::uint64_t length = 0;
        try {
            length = parse_integer<std::uint64_t>(item);
        } catch (const invalid_data_error&) {
            throw_invalid_geometry(text,
                                   " is not of the form dim1:...:dimN:channels, whose items are unsigned integers");
        }
        if (length == 0) {
            throw_invalid_geometry(text, " has a length of zero");
        }
        if (length > std::numeric_limits<std::uint64_t>::max() / samples) {
            throw_invalid_geometry(text, " has more samples than 64 bits can count");
        }
        samples *= length;
        if (colon == std::string_view::npos) {
            result.channels = length;
            break;
        }
        result.dimensions.push_back(length);
        start = colon + 1;
    }
    if (result.dimensions.empty()) {
        throw_invalid_geometry(text, " has no dimension, only a number of channels");
    }
    return result;
}

std::optional<std::uint64_t> pixel_data_size(const geometry& size, sample_format format) noexcept
{
    std::uint64_t bytes = sample_size(format);
    for (const std::uint64_t length : size.dimensions) {
        if (length != 0 && bytes > std::numeric_limits<std::uint64_t>::max() / length) {
            return std::nullopt;
        }
        bytes *= length;
    }
    if (size.channels != 0 && bytes > std::numeric_limits<std::uint64_t>::max() / size.channels) {
        return std::nullopt;
    }
    return bytes * size.channels;
}

sample_format parse_sample_format(std::string_view text)
{
    if (const std::optional<sample_format> format = named(text, sample_formats, sample_format_name)) {
        return *format;
    }
    throw unsupported_error(errc::unsupported_sample_format, quote(text) + " is not a sample format");
}

color_space parse_color_space(std::string_view text)
{
    if (const std::optional<color_space> space = named(text, color_spaces, color_space_name)) {
        return *space;
    }
    throw unsupported_error(errc::unsupported_color_space, quote(text) + " is not a colour space");
}

pixel_storage parse_pixel_storage(std::string_view text)
{
    if (const std::optional<pixel_storage> storage = named(text, storages, pixel_storage_name)) {
        return *storage;
    }
    throw invalid_data_error(errc::invalid_image, quote(text) + " is neither Planar nor Normal pixel storage");
}

image_type parse_image_type(std::string_view text)
{
    if (const std::optional<image_type> type = named(text, image_types, image_type_name)) {
        return *type;
    }
    throw invalid_data_error(errc::invalid_image, quote(text) + " is not an image type");
}

orientation parse_orientation(std::string_view text)
{
    if (const std::optional<orientation> turn = named(text, orientations, orientation_name)) {
        return *turn;
    }
    throw invalid_data_error(errc::invalid_image, quote(text) + " is not an orientation");
}

bounds parse_bounds(std::string_view text)
{
    const std::size_t colon = text.find(':');
    const std::optional<double> lower =
        colon == std::string_view::npos ? std::nullopt : finite_float(text.substr(0, colon));
    const std::optional<double> upper =
        colon == std::string_view::npos ? std::nullopt : finite_float(text.substr(colon + 1));
    if (!lower || !upper) {
        throw invalid_data_error(errc::invalid_image, "the bounds " + quote(text) +
                                                          " are not of the form lower:upper, two finite floating "
                                                          "point values");
    }
    if (*lower >= *upper) {
        throw invalid_data_error(errc::invalid_image,
                                 "the bounds " + quote(text) + " do not have the lower bound below the upper one");
    }
    return {.lower = *lower, .upper = *upper};
}

double parse_offset(std::string_view text)
{
    if (const std::optional<double> value = finite_float(text)) {
        return *value;
    }
    throw invalid_data_error(errc::invalid_image,
                             "the offset " + quote(text) + " is not a finite floating point value");
}

} // namespace openxisf::detail
