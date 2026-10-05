// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/export.h>
#include <openxisf/image.h>

#include <array>
#include <cstddef>
#include <optional>
#include <span>

/// @file
/// The colour transformations of Annex B of the specification: RGB, CIE XYZ, CIE L*a*b* and grayscale components.

namespace openxisf {

/// Three colour components, in the order of their colour space: R, G and B; X, Y and Z; or L, a and b.
using color_components = std::array<double, 3>;

/// The luminance coefficients that the chromaticity coordinates of three primaries give relative to the D50 reference
/// white (spec §8.5.4.1), red, green and blue, as an RGB working space needs them. Empty when the coordinates define no
/// RGB working space: a y coordinate of zero, or a singular system.
[[nodiscard]] OPENXISF_API std::optional<std::array<double, 3>>
luminance_coefficients(const std::array<double, 3>& x, const std::array<double, 3>& y) noexcept;

/// The normalized CIE L*a*b* components (equations [52] and [56] to [60]) of CIE XYZ tristimulus values relative to
/// D50, clipped to [0, 1].
[[nodiscard]] OPENXISF_API color_components xyz_to_lab(const color_components& xyz) noexcept;

/// The CIE XYZ tristimulus values relative to D50 of normalized CIE L*a*b* components (equations [61] to [63]), which
/// are clipped to [0, 1] first. Colours outside the gamut of RGB have values outside the range of in-gamut colours.
[[nodiscard]] OPENXISF_API color_components lab_to_xyz(const color_components& lab) noexcept;

/// The colour transformations of Annex B relative to one RGB working space.
///
/// RGB, CIE L*a*b* and grayscale components are nominal values in [0, 1]: the functions clip the components they are
/// given to that range, and clip their results to it, as Annex B requires. CIE L*a*b* components are normalized as XISF
/// units store them (equation [59]): L* / 100, and a* and b* mapped so that the achromatic axis is at 1/2. Tristimulus
/// values are relative to the D50 reference white and are not clipped.
///
/// The luminance coefficients are derived from the chromaticities of the working space, as Annex B requires, so the
/// luminance member of the working space is not used. A converter is a plain value, safe to use from any number of
/// threads at once.
class OPENXISF_API color_converter
{
public:
    /// The transformations relative to space, sRGB by default.
    /// @throws usage_error (errc::invalid_rgb_working_space) when the chromaticities are not in [0, 1] or define no RGB
    ///         working space, or the gamma is not a finite value above zero.
    explicit color_converter(const rgb_working_space& space = {});

    /// The linear component of an RGB component: equation [48], or the sRGB function of equation [49].
    [[nodiscard]] double linearize(double component) const noexcept;

    /// The RGB component of a linear component: equation [54], or the sRGB function of equation [55].
    [[nodiscard]] double delinearize(double component) const noexcept;

    /// The CIE XYZ tristimulus values of RGB components (equations [48] to [51]).
    [[nodiscard]] color_components rgb_to_xyz(const color_components& rgb) const noexcept;

    /// The RGB components of CIE XYZ tristimulus values (equations [53] to [55]).
    [[nodiscard]] color_components xyz_to_rgb(const color_components& xyz) const noexcept;

    /// The normalized CIE L*a*b* components of RGB components: rgb_to_xyz(), then xyz_to_lab().
    [[nodiscard]] color_components rgb_to_lab(const color_components& rgb) const noexcept;

    /// The RGB components of normalized CIE L*a*b* components: lab_to_xyz(), then xyz_to_rgb().
    [[nodiscard]] color_components lab_to_rgb(const color_components& lab) const noexcept;

    /// The colorimetrically defined grayscale component of RGB components (equation [64]): the L component of CIE
    /// L*a*b*, from the CIE Y component alone.
    [[nodiscard]] double grayscale(const color_components& rgb) const noexcept;

    /// The matrix M of equation [51], row by row, which turns linear RGB components into tristimulus values.
    [[nodiscard]] const std::array<color_components, 3>& rgb_to_xyz_matrix() const noexcept
    {
        return to_xyz_;
    }

private:
    double gamma_ = 0.0; // 0 for the sRGB functions.
    std::array<color_components, 3> to_xyz_{};
    std::array<color_components, 3> to_rgb_{};
};

/// Converts the pixel data of a CIE L*a*b* image from RGB components to CIE L*a*b* components, in place, relative to
/// the RGB working space of the image, sRGB when it has none: what an encoder does before it writes the image (spec
/// §8.5.4.1). The first three channels are converted and the alpha channels are left as they are.
///
/// image describes the data as the image to write: it is a CIE L*a*b* image of three nominal channels or more, of a
/// real sample format, in its pixel storage model. Samples are mapped from its representable range to [0, 1] and back
/// (spec §8.5.5, equation [4]), and integer samples are rounded to the nearest value.
/// @throws usage_error (errc::invalid_argument) when image is not a CIE L*a*b* image of three channels or more and real
///         samples with a representable range, or pixels does not have image.data_size() bytes; and
///         (errc::invalid_rgb_working_space) when its RGB working space is not valid.
OPENXISF_API void convert_rgb_to_lab(std::span<std::byte> pixels, const image_info& image);

/// Converts the pixel data of a CIE L*a*b* image to RGB components, in place: what a decoder does after it reads the
/// image (spec §8.5.4.1). The opposite of convert_rgb_to_lab(), with the same requirements.
/// @throws usage_error like convert_rgb_to_lab().
OPENXISF_API void convert_lab_to_rgb(std::span<std::byte> pixels, const image_info& image);

} // namespace openxisf
