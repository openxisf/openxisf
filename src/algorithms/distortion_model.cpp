// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "algorithms/distortion_model.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace openxisf::detail {

namespace {

// base^exponent, by squaring: a fixed sequence of products, so that every platform gives the same value.
double power(double base, std::uint32_t exponent) noexcept
{
    double result = 1.0;
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result *= base;
        }
        base *= base;
        exponent >>= 1U;
    }
    return result;
}

double square(double x) noexcept
{
    return x * x;
}

} // namespace

bool has_shape_parameter(basis_function basis) noexcept
{
    return basis == basis_function::gaussian || basis == basis_function::multiquadric ||
           basis == basis_function::inverse_multiquadric || basis == basis_function::inverse_quadratic;
}

double radial_basis(basis_function basis, std::int32_t order, double shape, double squared) noexcept
{
    switch (basis) {
    case basis_function::thin_plate_spline:
        // ρ² ln ρ = ρ² ln(ρ²) / 2, with φ(0) = 0.
        return squared > 0.0 ? 0.5 * squared * std::log(squared) : 0.0;
    case basis_function::variable_order:
        // (ρ²)^(m-1) ln ρ², with φ(0) = 0; the order is at least 3.
        return squared > 0.0 && order > 1 ? power(squared, static_cast<std::uint32_t>(order - 1)) * std::log(squared)
                                          : 0.0;
    case basis_function::gaussian:
        return std::exp(-(square(shape) * squared));
    case basis_function::multiquadric:
        return std::sqrt(1.0 + (square(shape) * squared));
    case basis_function::inverse_multiquadric:
        return 1.0 / std::sqrt(1.0 + (square(shape) * squared));
    case basis_function::inverse_quadratic:
        return 1.0 / (1.0 + (square(shape) * squared));
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double wendland(double t) noexcept
{
    if (!(t < 1.0)) {
        return 0.0;
    }
    const double complement = square(1.0 - t);
    return complement * complement * ((4.0 * t) + 1.0);
}

std::uint64_t polynomial_size(std::int32_t order) noexcept
{
    if (order < 1) {
        return 0;
    }
    const auto m = static_cast<std::uint64_t>(order);
    return m * (m + 1) / 2;
}

double polynomial_value(std::span<const double> coefficients, std::int32_t order, double xi, double eta) noexcept
{
    // Each degree d is the homogeneous polynomial c0 ξ^d + c1 ξ^(d-1) η + ... + cd η^d, evaluated by Horner's scheme in
    // ξ with the powers of η: s ← s ξ + cb η^b.
    double total = 0.0;
    std::size_t first = 0;
    for (std::int32_t degree = 0; degree < order; ++degree) {
        const auto count = static_cast<std::size_t>(degree) + 1;
        double value = coefficients[first];
        double eta_power = 1.0;
        for (std::size_t b = 1; b < count; ++b) {
            eta_power *= eta;
            value = (value * xi) + (coefficients[first + b] * eta_power);
        }
        total += value;
        first += count;
    }
    return total;
}

double distortion_model::spline_value(const surface_spline& spline, double x, double y) const noexcept
{
    const double xi = spline.normalization[2] * (x - spline.normalization[0]);
    const double eta = spline.normalization[2] * (y - spline.normalization[1]);
    const std::size_t count = spline.nodes.size() / 2;
    double sum = 0.0;
    for (std::size_t k = 0; k < count; ++k) {
        const double squared = square(xi - spline.nodes[2 * k]) + square(eta - spline.nodes[(2 * k) + 1]);
        sum += spline.coefficients[k] * radial_basis(basis, order, spline.shape, squared);
    }
    if (polynomial) {
        sum += polynomial_value(std::span(spline.coefficients).subspan(count), order, xi, eta);
    }
    return sum;
}

std::array<double, 2> distortion_model::term_value(const distortion_term& term, double x, double y) const noexcept
{
    if (!term.shared) {
        return {spline_value(term.x, x, y), spline_value(term.y, x, y)};
    }
    // The components share their nodes, so one pass computes each radial function once for both.
    const surface_spline& spline = term.x;
    const double xi = spline.normalization[2] * (x - spline.normalization[0]);
    const double eta = spline.normalization[2] * (y - spline.normalization[1]);
    const std::size_t count = spline.nodes.size() / 2;
    double sum_x = 0.0;
    double sum_y = 0.0;
    for (std::size_t k = 0; k < count; ++k) {
        const double squared = square(xi - spline.nodes[2 * k]) + square(eta - spline.nodes[(2 * k) + 1]);
        const double radial = radial_basis(basis, order, spline.shape, squared);
        sum_x += spline.coefficients[k] * radial;
        sum_y += term.y.coefficients[k] * radial;
    }
    if (polynomial) {
        sum_x += polynomial_value(std::span(spline.coefficients).subspan(count), order, xi, eta);
        sum_y += polynomial_value(std::span(term.y.coefficients).subspan(count), order, xi, eta);
    }
    return {sum_x, sum_y};
}

std::array<double, 2> distortion_model::residual(double x, double y) const noexcept
{
    std::array<double, 2> weighted{};
    double total = 0.0;
    double coverage = 0.0;
    const distortion_term* nearest = nullptr;
    double nearest_distance = std::numeric_limits<double>::infinity();
    const distortion_term* fallback = nullptr;
    const auto add = [&](const distortion_term& term, double weight) {
        const std::array<double, 2> value = term_value(term, x, y);
        weighted[0] += weight * value[0];
        weighted[1] += weight * value[1];
        total += weight;
    };
    for (const distortion_term& term : terms) {
        switch (term.kind) {
        case term_kind::local: {
            const double t = std::hypot(x - term.center[0], y - term.center[1]) / term.radius;
            if (t < nearest_distance) {
                nearest_distance = t;
                nearest = &term;
            }
            const double weight = wendland(t);
            if (weight > 0.0) {
                add(term, weight);
                coverage += weight;
            }
            break;
        }
        case term_kind::global:
            add(term, 1.0);
            break;
        case term_kind::fallback:
            fallback = &term;
            break;
        }
    }
    // The Fallback term takes over where the coverage of the Local terms fades, with the weight W(s / t0).
    if (fallback != nullptr) {
        const double weight = wendland(coverage / threshold);
        if (weight > 0.0) {
            add(*fallback, weight);
        }
    }
    if (total == 0.0) {
        // No term covers the point, which only happens without a Global or Fallback term: the value of the Local term
        // with the smallest t, or zero without terms.
        return nearest != nullptr ? term_value(*nearest, x, y) : std::array<double, 2>{};
    }
    return {weighted[0] / total, weighted[1] / total};
}

} // namespace openxisf::detail
