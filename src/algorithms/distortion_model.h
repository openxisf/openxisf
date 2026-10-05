// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// The distortion models of astrometric solutions (spec §11.5.3.7.4): a residual field added to a projective
// transformation, made of the normalized weighted sum of vector-valued surface splines, one for each term.

namespace openxisf::detail {

/// The basis functions of the splines of a distortion model (spec §11.5.3.7.4.3).
enum class basis_function : std::uint8_t
{
    thin_plate_spline,
    variable_order,
    gaussian,
    multiquadric,
    inverse_multiquadric,
    inverse_quadratic,
};

/// True for the basis functions that have a shape parameter.
[[nodiscard]] bool has_shape_parameter(basis_function basis) noexcept;

/// φ of a basis function (Table 17) at a distance ρ, given as ρ², for a spline of the given order and shape parameter.
[[nodiscard]] double radial_basis(basis_function basis, std::int32_t order, double shape, double squared) noexcept;

/// The Wendland C² function W(t) of equation [26]: (1 - t)^4 (4t + 1) below 1, and 0 from 1 on.
[[nodiscard]] double wendland(double t) noexcept;

/// The number of polynomial coefficients of a spline of order m, which is at least 1: m(m + 1)/2 (equation [27]).
[[nodiscard]] std::uint64_t polynomial_size(std::int32_t order) noexcept;

/// The polynomial part of a spline of order m at a normalized point (ξ, η) (equations [27] and [28]): its coefficients,
/// of the monomials ordered by total degree and then by descending power of ξ.
[[nodiscard]] double polynomial_value(std::span<const double> coefficients, std::int32_t order, double xi,
                                      double eta) noexcept;

/// A scalar surface spline (spec §11.5.3.7.4.2).
struct surface_spline
{
    /// x0, y0 and r0: a point p of the source coordinates is the normalized point r0 (p - (x0, y0)).
    std::array<double, 3> normalization{};
    /// The normalized coordinates of the nodes, ξ and η of each in turn.
    std::vector<double> nodes{};
    /// A radial coefficient for each node, then the polynomial coefficients.
    std::vector<double> coefficients{};
    /// ε, for a basis function that has one.
    double shape = 0.0;
};

/// The kinds of terms of a distortion model (spec §11.5.3.7.4.1).
enum class term_kind : std::uint8_t
{
    global,
    local,
    fallback,
};

/// A term of a distortion model: the splines of its X and Y components, and the disc of the support of a Local term.
struct distortion_term
{
    term_kind kind = term_kind::global;
    std::array<double, 2> center{};
    double radius = 0.0;
    surface_spline x{};
    surface_spline y{};
    /// The Y spline has the nodes, normalization and shape parameter of the X spline, and its own nodes are empty.
    bool shared = false;
};

/// The distortion model of one direction of the image-plane step (spec §11.5.3.7.4), whose properties are valid.
struct distortion_model
{
    basis_function basis = basis_function::thin_plate_spline;
    std::int32_t order = 2;
    bool polynomial = true;
    /// The coverage threshold t0 of the Fallback term.
    double threshold = 0.0;
    /// The Local terms in order, then the Global and the Fallback term, when there are.
    std::vector<distortion_term> terms{};

    /// The value of a scalar spline at a point of the source coordinates (equation [27]).
    [[nodiscard]] double spline_value(const surface_spline& spline, double x, double y) const noexcept;

    /// The values of the X and Y splines of a term at a point of the source coordinates.
    [[nodiscard]] std::array<double, 2> term_value(const distortion_term& term, double x, double y) const noexcept;

    /// The residual field R_D at a point of the source coordinates (equation [25]).
    [[nodiscard]] std::array<double, 2> residual(double x, double y) const noexcept;
};

} // namespace openxisf::detail
