// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/export.h>
#include <openxisf/image.h>

#include <cstddef>
#include <span>

/// @file
/// Display functions (spec §8.5.6): their evaluation, and the adaptive algorithm of spec §8.5.7 that computes one from
/// the statistics of an image.

namespace openxisf {

/// The midtones transfer function M(x; m) of equation [5], for a sample x and a midtones balance m, both in [0, 1]. It
/// maps 0 to 0, 1 to 1, and m to 1/2 when m is neither.
[[nodiscard]] OPENXISF_API double midtones_transfer(double x, double midtones) noexcept;

/// The value of a display function at a sample x normalized to [0, 1] (equation [8]): the clipping function of equation
/// [6], then the midtones transfer function, then the expansion function of equation [7]. component selects the
/// parameters: 0 for red or gray, 1 for green, 2 for blue and 3 for the lightness of colour images. The parameters must
/// satisfy the constraints of spec §8.5.6, as those of a unit that the reader opened do.
/// @throws usage_error when component is above 3.
[[nodiscard]] OPENXISF_API double apply_display_function(const display_function& function, std::size_t component,
                                                         double x);

/// The parameters of the adaptive display function algorithm (spec §8.5.7).
struct adaptive_display_options
{
    /// The target mean background B, in [0, 1]. It sets the overall brightness of the displayed image.
    double target_background = 0.25;
    /// The clipping point C, in units of the normalized median absolute deviation from the median, at most 0. It sets
    /// the overall contrast of the displayed image.
    double clipping = -2.8;
    /// One set of parameters for the three channels of an RGB image (equations [15] to [18]), which keeps its colour
    /// balance, instead of one for each channel, which tends to align their histogram peaks.
    bool linked = false;
};

/// Computes the display function of an image with the adaptive algorithm of spec §8.5.7, from the median and the
/// normalized median absolute deviation (equation [10]) of each nominal channel of its pixel data.
///
/// image describes the data: a Gray or RGB image of a real sample format, with a representable range, in its pixel
/// storage model. Samples are mapped from the representable range to [0, 1] (equation [4]) before the statistics,
/// NaN samples are left out, and the median of an even number of samples is the mean of the two middle ones. The
/// parameters of the components that the image does not have, the lightness among them, are those of the identity, and
/// so are those of a channel without samples other than NaN. The algorithm copies one channel at a time.
/// @throws usage_error when image is not such an image, pixels does not have image.data_size() bytes, or the options
///         are out of range.
[[nodiscard]] OPENXISF_API display_function adaptive_display_function(std::span<const std::byte> pixels,
                                                                      const image_info& image,
                                                                      const adaptive_display_options& options = {});

} // namespace openxisf
