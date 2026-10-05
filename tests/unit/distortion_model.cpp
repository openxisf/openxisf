// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The distortion models of astrometric solutions (spec §11.5.3.7.4): basis functions, splines, and the weighting of
// their terms.

#include "algorithms/distortion_model.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace {

namespace detail = openxisf::detail;
using detail::basis_function;
using detail::distortion_model;
using detail::distortion_term;
using detail::surface_spline;
using detail::term_kind;

// A spline of order 1 without nodes: the constant of its single polynomial coefficient.
surface_spline constant(double value)
{
    return {.normalization = {0.0, 0.0, 1.0}, .nodes = {}, .coefficients = {value}, .shape = 0.0};
}

distortion_term constant_term(term_kind kind, double x, double y)
{
    return {.kind = kind, .center = {}, .radius = 0.0, .x = constant(x), .y = constant(y), .shared = false};
}

distortion_term local_term(double cx, double cy, double radius, double x, double y)
{
    distortion_term term = constant_term(term_kind::local, x, y);
    term.center = {cx, cy};
    term.radius = radius;
    return term;
}

distortion_model constant_model(std::vector<distortion_term> terms, double threshold = 0.0)
{
    return {.basis = basis_function::thin_plate_spline,
            .order = 1,
            .polynomial = true,
            .threshold = threshold,
            .terms = std::move(terms)};
}

// -----------------------------------------------------------------------------------------------------------------
// Basis functions (spec §11.5.3.7.4.3) and weights (spec §11.5.3.7.4.1)

TEST(distortion_model, the_basis_functions_have_the_values_of_table_17)
{
    using detail::radial_basis;
    // ρ = 2: ρ² ln ρ, and (ρ²)^(m-1) ln ρ² for m = 3 and 4.
    EXPECT_DOUBLE_EQ(radial_basis(basis_function::thin_plate_spline, 2, 0.0, 4.0), 4.0 * std::numbers::ln2);
    EXPECT_DOUBLE_EQ(radial_basis(basis_function::thin_plate_spline, 5, 0.0, 4.0), 4.0 * std::numbers::ln2);
    EXPECT_DOUBLE_EQ(radial_basis(basis_function::variable_order, 3, 0.0, 4.0), 16.0 * 2.0 * std::numbers::ln2);
    EXPECT_DOUBLE_EQ(radial_basis(basis_function::variable_order, 4, 0.0, 4.0), 64.0 * 2.0 * std::numbers::ln2);
    EXPECT_EQ(radial_basis(basis_function::thin_plate_spline, 2, 0.0, 0.0), 0.0);
    EXPECT_EQ(radial_basis(basis_function::variable_order, 3, 0.0, 0.0), 0.0);
    // ε = 2 and ρ = 1/2, so (ερ)² = 1.
    EXPECT_DOUBLE_EQ(radial_basis(basis_function::gaussian, 2, 2.0, 0.25), std::exp(-1.0));
    EXPECT_DOUBLE_EQ(radial_basis(basis_function::multiquadric, 2, 2.0, 0.25), std::numbers::sqrt2);
    EXPECT_DOUBLE_EQ(radial_basis(basis_function::inverse_multiquadric, 2, 2.0, 0.25), 1.0 / std::numbers::sqrt2);
    EXPECT_DOUBLE_EQ(radial_basis(basis_function::inverse_quadratic, 2, 2.0, 0.25), 0.5);
    EXPECT_EQ(radial_basis(basis_function::gaussian, 2, 2.0, 0.0), 1.0);

    EXPECT_FALSE(detail::has_shape_parameter(basis_function::thin_plate_spline));
    EXPECT_FALSE(detail::has_shape_parameter(basis_function::variable_order));
    EXPECT_TRUE(detail::has_shape_parameter(basis_function::gaussian));
    EXPECT_TRUE(detail::has_shape_parameter(basis_function::multiquadric));
    EXPECT_TRUE(detail::has_shape_parameter(basis_function::inverse_multiquadric));
    EXPECT_TRUE(detail::has_shape_parameter(basis_function::inverse_quadratic));
}

TEST(distortion_model, the_weights_of_local_terms_follow_the_wendland_function)
{
    // W(t) = (1 - t)^4 (4t + 1) below 1.
    EXPECT_EQ(detail::wendland(0.0), 1.0);
    EXPECT_EQ(detail::wendland(0.5), 0.1875);
    EXPECT_EQ(detail::wendland(0.25), 0.6328125);
    EXPECT_EQ(detail::wendland(1.0), 0.0);
    EXPECT_EQ(detail::wendland(2.5), 0.0);
}

// -----------------------------------------------------------------------------------------------------------------
// Splines (spec §11.5.3.7.4.2)

TEST(distortion_model, the_polynomial_part_orders_monomials_by_degree_and_then_by_descending_power_of_xi)
{
    // Equation [28] at ξ = 2 and η = 3: 1, ξ, η, ξ², ξη, η², ξ³, ξ²η, ξη², η³.
    const std::array<double, 10> monomials{1.0, 2.0, 3.0, 4.0, 6.0, 9.0, 8.0, 12.0, 18.0, 27.0};
    EXPECT_EQ(detail::polynomial_size(1), 1U);
    EXPECT_EQ(detail::polynomial_size(2), 3U);
    EXPECT_EQ(detail::polynomial_size(3), 6U);
    EXPECT_EQ(detail::polynomial_size(4), 10U);
    for (std::size_t j = 0; j < monomials.size(); ++j) {
        std::array<double, 10> coefficients{};
        coefficients[j] = 1.0;
        EXPECT_EQ(detail::polynomial_value(coefficients, 4, 2.0, 3.0), monomials[j]) << j;
    }
    EXPECT_EQ(detail::polynomial_value(std::array<double, 3>{0.5, -1.0, 2.0}, 2, 2.0, 3.0), 0.5 - 2.0 + 6.0);
}

TEST(distortion_model, a_spline_is_the_sum_of_its_radial_and_polynomial_parts_at_the_normalized_point)
{
    // One node, normalization (100, 50, 0.01) and order 2: at (130, 20) the normalized point is (0.3, -0.3).
    const surface_spline spline{.normalization = {100.0, 50.0, 0.01},
                                .nodes = {0.5, -0.25},
                                .coefficients = {2.0, 0.5, -1.0, 3.0},
                                .shape = 0.0};
    const distortion_model model{
        .basis = basis_function::thin_plate_spline, .order = 2, .polynomial = true, .threshold = 0.0, .terms = {}};
    const double xi = 0.01 * (130.0 - 100.0);
    const double eta = 0.01 * (20.0 - 50.0);
    const double rho = std::hypot(xi - 0.5, eta + 0.25);
    const double expected = (2.0 * rho * rho * std::log(rho)) + 0.5 - (1.0 * xi) + (3.0 * eta);
    EXPECT_NEAR(model.spline_value(spline, 130.0, 20.0), expected, 1e-15);

    // Without a polynomial part, the coefficients are the radial ones alone.
    const surface_spline radial{
        .normalization = {100.0, 50.0, 0.01}, .nodes = {0.5, -0.25}, .coefficients = {2.0}, .shape = 2.0};
    const distortion_model gaussian{
        .basis = basis_function::gaussian, .order = 1, .polynomial = false, .threshold = 0.0, .terms = {}};
    EXPECT_NEAR(gaussian.spline_value(radial, 130.0, 20.0), 2.0 * std::exp(-4.0 * rho * rho), 1e-15);
}

TEST(distortion_model, a_y_component_that_shares_the_nodes_of_x_gives_what_its_own_copy_would)
{
    const surface_spline x{.normalization = {10.0, 20.0, 0.5},
                           .nodes = {0.1, 0.2, -0.3, 0.4, 0.7, -0.6},
                           .coefficients = {1.5, -2.0, 0.25, 0.1, -0.2, 0.3},
                           .shape = 0.0};
    surface_spline y = x;
    y.coefficients = {-0.5, 1.0, 2.0, -0.1, 0.05, 0.2};
    const distortion_model model{
        .basis = basis_function::thin_plate_spline, .order = 2, .polynomial = true, .threshold = 0.0, .terms = {}};
    const distortion_term own{.kind = term_kind::global, .center = {}, .radius = 0.0, .x = x, .y = y, .shared = false};
    surface_spline coefficients_only = y;
    coefficients_only.nodes.clear();
    const distortion_term shared{
        .kind = term_kind::global, .center = {}, .radius = 0.0, .x = x, .y = coefficients_only, .shared = true};
    for (const std::array<double, 2> point :
         {std::array<double, 2>{10.0, 20.0}, std::array<double, 2>{11.3, 18.9}, std::array<double, 2>{-4.0, 33.0}}) {
        const std::array<double, 2> expected = model.term_value(own, point[0], point[1]);
        const std::array<double, 2> actual = model.term_value(shared, point[0], point[1]);
        EXPECT_EQ(actual[0], expected[0]);
        EXPECT_EQ(actual[1], expected[1]);
        EXPECT_EQ(expected[0], model.spline_value(x, point[0], point[1]));
        EXPECT_EQ(expected[1], model.spline_value(y, point[0], point[1]));
    }
}

// -----------------------------------------------------------------------------------------------------------------
// The residual field (equation [25])

TEST(distortion_model, a_global_term_is_the_residual_field_everywhere)
{
    const distortion_model model = constant_model({constant_term(term_kind::global, 0.25, -0.5)});
    EXPECT_EQ(model.residual(0.0, 0.0), (std::array<double, 2>{0.25, -0.5}));
    EXPECT_EQ(model.residual(-1e6, 3e5), (std::array<double, 2>{0.25, -0.5}));
}

TEST(distortion_model, overlapping_local_terms_are_averaged_with_their_weights)
{
    // Two discs of radius 10 centred 8 apart; at (2, 0) the first gives t = 0.2 and the second t = 0.6.
    const distortion_model model =
        constant_model({local_term(0.0, 0.0, 10.0, 1.0, 10.0), local_term(8.0, 0.0, 10.0, 3.0, 20.0)});
    const double w1 = detail::wendland(0.2);
    const double w2 = detail::wendland(0.6);
    const std::array<double, 2> value = model.residual(2.0, 0.0);
    EXPECT_DOUBLE_EQ(value[0], ((w1 * 1.0) + (w2 * 3.0)) / (w1 + w2));
    EXPECT_DOUBLE_EQ(value[1], ((w1 * 10.0) + (w2 * 20.0)) / (w1 + w2));
    // Inside one disc only, the residual is that term.
    EXPECT_EQ(model.residual(-5.0, 0.0), (std::array<double, 2>{1.0, 10.0}));
    // Global terms add their values with the weight 1.
    distortion_model with_global = model;
    with_global.terms.push_back(constant_term(term_kind::global, 5.0, 0.0));
    EXPECT_DOUBLE_EQ(with_global.residual(2.0, 0.0)[0], ((w1 * 1.0) + (w2 * 3.0) + 5.0) / (w1 + w2 + 1.0));
}

TEST(distortion_model, without_coverage_the_residual_is_the_nearest_local_term_or_zero)
{
    // Beyond both discs, the term with the smallest t: 30 / 10 against 22 / 5.
    const distortion_model model =
        constant_model({local_term(0.0, 0.0, 10.0, 1.0, 10.0), local_term(52.0, 0.0, 5.0, 3.0, 20.0)});
    EXPECT_EQ(model.residual(30.0, 0.0), (std::array<double, 2>{1.0, 10.0}));
    EXPECT_EQ(model.residual(40.0, 0.0), (std::array<double, 2>{3.0, 20.0}));
    EXPECT_EQ(constant_model({}).residual(1.0, 2.0), (std::array<double, 2>{0.0, 0.0}));
}

TEST(distortion_model, the_fallback_term_takes_over_where_the_coverage_of_local_terms_fades)
{
    // One Local term of radius 10 at the origin, and a Fallback term with the threshold 0.5.
    const distortion_model model =
        constant_model({local_term(0.0, 0.0, 10.0, 1.0, 2.0), constant_term(term_kind::fallback, 100.0, 200.0)}, 0.5);
    // Covered: s = W(0.1) ≥ t0, so the Fallback term has no weight.
    EXPECT_EQ(model.residual(1.0, 0.0), (std::array<double, 2>{1.0, 2.0}));
    // No coverage: the Fallback term alone.
    EXPECT_EQ(model.residual(50.0, 0.0), (std::array<double, 2>{100.0, 200.0}));
    // In between, its weight is W(s / t0).
    const double s = detail::wendland(0.8);
    const double fallback = detail::wendland(s / 0.5);
    ASSERT_GT(fallback, 0.0);
    const std::array<double, 2> value = model.residual(8.0, 0.0);
    EXPECT_DOUBLE_EQ(value[0], ((s * 1.0) + (fallback * 100.0)) / (s + fallback));
    EXPECT_DOUBLE_EQ(value[1], ((s * 2.0) + (fallback * 200.0)) / (s + fallback));
}

} // namespace
