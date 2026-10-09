// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "algorithms/pixel_values.h"

#include <openxisf/error.h>

#include <cmath>
#include <optional>
#include <string>

namespace openxisf::detail {

bounds checked_range(const image_info& image, std::size_t pixel_bytes, std::string_view what)
{
    const std::string prefix(what);
    if (image.sample_format == sample_format::complex32 || image.sample_format == sample_format::complex64) {
        // Spec §8.5.5: complex images have no representable range.
        throw usage_error(errc::invalid_argument, prefix + " needs real samples, and the image is complex");
    }
    const std::optional<bounds> range = image.representable_range();
    if (!range) {
        throw usage_error(errc::invalid_argument, prefix + " needs the bounds of a floating point image");
    }
    // Equation [4] divides by the width of the range, which two finite bounds far apart, such as ±1e308, overflow.
    if (!std::isfinite(range->lower) || !std::isfinite(range->upper) || range->lower >= range->upper ||
        !std::isfinite(range->upper - range->lower)) {
        throw usage_error(errc::invalid_argument,
                          prefix + " needs bounds that are finite and in increasing order, with a finite width");
    }
    if (image.data_size() != pixel_bytes) {
        throw usage_error(errc::invalid_argument, prefix + " needs pixel data of " + std::to_string(image.data_size()) +
                                                      " bytes, the size of the image, and has " +
                                                      std::to_string(pixel_bytes));
    }
    return *range;
}

} // namespace openxisf::detail
