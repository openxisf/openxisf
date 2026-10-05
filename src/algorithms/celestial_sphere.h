// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/astrometry.h>

#include <numbers>
#include <optional>

// The deprojection, projection and spherical rotation steps of astrometric solutions (spec §11.5.3.7.1), as Annex A
// restates them from the WCS formulation of celestial coordinate systems. Every angle is in degrees.

namespace openxisf::detail {

/// R of Annex A: degrees in a radian, the radius of the sphere on which the projections are defined.
inline constexpr double degrees_per_radian = 180.0 / std::numbers::pi;

/// The sine of an angle in degrees, exact at multiples of 90.
[[nodiscard]] double sin_degrees(double angle) noexcept;

/// The cosine of an angle in degrees, exact at multiples of 90.
[[nodiscard]] double cos_degrees(double angle) noexcept;

/// The two-argument arctangent of Annex A.1: the angle in (-180, 180] whose cosine and sine are proportional to x and
/// y.
[[nodiscard]] double arg(double x, double y) noexcept;

/// An angle reduced to (-180, 180], as longitudes are.
[[nodiscard]] double reduce_longitude(double angle) noexcept;

/// An angle reduced to [0, 360), as right ascensions are.
[[nodiscard]] double reduce_right_ascension(double angle) noexcept;

/// Native spherical coordinates: longitude φ and latitude θ.
struct native_point
{
    double phi = 0.0;
    double theta = 0.0;
};

/// Projection plane coordinates (u, v).
struct plane_point
{
    double u = 0.0;
    double v = 0.0;
};

/// True for the zenithal projections, whose plane is tangent at, or centred on, the native pole.
[[nodiscard]] bool is_zenithal(projection_system system) noexcept;

/// The default native coordinates of the reference point (spec §11.5.3.7.2): (0, 90) for zenithal projections, and
/// (0, 0) for the others.
[[nodiscard]] native_point default_reference_native(projection_system system) noexcept;

/// The default native coordinates of the celestial pole (Annex A.2): the longitude φ0 when δ0 ≥ θ0 and φ0 + 180
/// otherwise, reduced to (-180, 180], and the latitude 90.
[[nodiscard]] native_point default_pole_native(double reference_dec, native_point reference_native) noexcept;

/// The spherical rotation between native and celestial coordinates (Annex A.2), through the celestial coordinates of
/// the native pole.
class spherical_rotation
{
public:
    /// The rotation of a zenithal projection about the reference point (0, 0), with the default native coordinates.
    spherical_rotation() noexcept = default;

    /// The rotation that takes the reference point, of celestial coordinates reference, to the native coordinates
    /// reference_native, with the celestial pole at the native longitude pole_native.phi; pole_native.theta selects one
    /// of two solutions (Annex A.2.1). Empty when the parameters define no rotation.
    [[nodiscard]] static std::optional<spherical_rotation>
    make(celestial_point reference, native_point reference_native, native_point pole_native) noexcept;

    /// The celestial coordinates of a native point (equation [34]): a right ascension in [0, 360).
    [[nodiscard]] celestial_point to_celestial(native_point point) const noexcept;

    /// The native coordinates of a celestial point (equation [35]): a longitude in (-180, 180].
    [[nodiscard]] native_point to_native(celestial_point point) const noexcept;

    /// The celestial coordinates of the native pole, αp reduced to the sign of the right ascension of the reference
    /// point.
    [[nodiscard]] celestial_point native_pole() const noexcept
    {
        return {.ra = alpha_p_, .dec = delta_p_};
    }

private:
    spherical_rotation(double alpha_p, double delta_p, double phi_p) noexcept;

    double alpha_p_ = 0.0;
    double delta_p_ = 0.0;
    double phi_p_ = 180.0;
    // The native pole is a celestial pole, where both transformations reduce to equation [36].
    bool polar_ = false;
};

/// The projection plane coordinates of a native point (Annex A.3 to A.5), or empty when it is beyond the domain of the
/// projection.
[[nodiscard]] std::optional<plane_point> project(projection_system system, native_point point) noexcept;

/// The native coordinates of a point of the projection plane, the inverse of project(), or empty when the point is
/// beyond the boundary of the projection.
[[nodiscard]] std::optional<native_point> deproject(projection_system system, plane_point point) noexcept;

} // namespace openxisf::detail
