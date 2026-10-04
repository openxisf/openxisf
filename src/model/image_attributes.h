// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/types.h>

#include <cstdint>
#include <optional>
#include <string_view>

// The attributes of Image elements (spec §11.5.1, §11.5.2), read from their text. Thumbnail elements have the same
// attributes (spec §11.12). Names are case-sensitive.

namespace openxisf::detail {

/// The geometry of a geometry attribute: dim1:...:dimN:channel-count, with N >= 1, each item an unsigned integer of
/// spec §8.3 above zero, white space around it ignored. Throws invalid_data_error with errc::invalid_geometry when the
/// text is not of that form, or when its number of samples does not fit in 64 bits.
[[nodiscard]] geometry parse_geometry(std::string_view text);

/// The size in bytes of the pixel data of an image of this geometry and sample format, or nothing when it does not fit
/// in 64 bits.
[[nodiscard]] std::optional<std::uint64_t> pixel_data_size(const geometry& size, sample_format format) noexcept;

/// The sample format of a sampleFormat attribute. Throws unsupported_error with errc::unsupported_sample_format for a
/// name that the specification does not define.
[[nodiscard]] sample_format parse_sample_format(std::string_view text);

/// The colour space of a colorSpace attribute. Throws unsupported_error with errc::unsupported_color_space for a name
/// that the specification does not define.
[[nodiscard]] color_space parse_color_space(std::string_view text);

/// The storage model of a pixelStorage attribute. Throws invalid_data_error with errc::invalid_image.
[[nodiscard]] pixel_storage parse_pixel_storage(std::string_view text);

/// The image type of an imageType attribute. Throws invalid_data_error with errc::invalid_image.
[[nodiscard]] image_type parse_image_type(std::string_view text);

/// The orientation of an orientation attribute. Throws invalid_data_error with errc::invalid_image.
[[nodiscard]] orientation parse_orientation(std::string_view text);

/// The bounds of a bounds attribute: lower:upper, two floating point values of spec §8.3.3, finite, with lower below
/// upper. Throws invalid_data_error with errc::invalid_image.
[[nodiscard]] bounds parse_bounds(std::string_view text);

/// The value of an offset attribute: a finite floating point value of spec §8.3.3. The specification requires it to be
/// at least zero, which the caller checks. Throws invalid_data_error with errc::invalid_image.
[[nodiscard]] double parse_offset(std::string_view text);

} // namespace openxisf::detail
