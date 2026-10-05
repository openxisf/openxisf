// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/astrometry.h>
#include <openxisf/property.h>

#include "algorithms/celestial_sphere.h"
#include "algorithms/distortion_model.h"

#include <array>
#include <optional>
#include <string>

// An astrometric solution read from the properties of the AstrometricSolution namespace (spec §11.5.3.7), and its
// evaluation through any of its layers.

namespace openxisf::detail {

/// A solution: whether each layer is available, why not, and what the available ones hold.
struct astrometric_model
{
    /// The value of AstrometricSolution:Version, when it is a String.
    std::string version{};
    /// The status of layers 1, 2 and 3.
    std::array<astrometric_status, 3> status{astrometric_status::absent, astrometric_status::absent,
                                             astrometric_status::absent};
    /// What makes each unavailable layer so.
    std::array<std::string, 3> problem{};
    /// Layer 1, and what its evaluation needs: the spherical rotation and the inverse of the linear transformation.
    std::optional<astrometric_projection> projection{};
    spherical_rotation rotation{};
    std::array<double, 4> inverse_linear{};
    /// Layer 2.
    std::optional<projective_transformations> projective{};
    /// Layer 3: the distortion model of each direction.
    std::optional<distortion_model> image_to_projection{};
    std::optional<distortion_model> projection_to_image{};
};

/// Reads the solution of properties. Invalid properties make layers unavailable; nothing is thrown for them.
[[nodiscard]] astrometric_model read_astrometric_model(const property_list& properties);

/// The celestial coordinates of a point of the image through a layer, which must be available, or empty when the point
/// has none.
[[nodiscard]] std::optional<celestial_point> image_to_celestial(const astrometric_model& model, image_point point,
                                                                astrometric_layer layer) noexcept;

/// The image coordinates of a point of the sky through a layer, which must be available, or empty when the point has
/// none.
[[nodiscard]] std::optional<image_point> celestial_to_image(const astrometric_model& model, celestial_point point,
                                                            astrometric_layer layer) noexcept;

} // namespace openxisf::detail
