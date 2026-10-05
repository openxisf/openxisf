// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <openxisf/color.h>
#include <openxisf/error.h>

#include "algorithms/pixel_values.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>

namespace openxisf {

namespace {

using matrix = std::array<color_components, 3>;

// The CIE XYZ tristimulus values of the D50 reference white, with Y = 1 (spec §8.5.4.1).
constexpr double d50_x = 0.96422;
constexpr double d50_z = 0.82521;

// Annex B.2, equation [57].
constexpr double epsilon = 216.0 / 24389.0;
constexpr double kappa = 24389.0 / 27.0;

double determinant(const matrix& m) noexcept
{
    return (m[0][0] * ((m[1][1] * m[2][2]) - (m[1][2] * m[2][1]))) -
           (m[0][1] * ((m[1][0] * m[2][2]) - (m[1][2] * m[2][0]))) +
           (m[0][2] * ((m[1][0] * m[2][1]) - (m[1][1] * m[2][0])));
}

// The inverse of m, whose determinant is whole and not zero: the transposed matrix of cofactors over the determinant.
matrix inverse(const matrix& m, double whole) noexcept
{
    matrix result{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            // The cofactor of element (column, row), from the rows and columns that follow it cyclically, which carry
            // the sign of the cofactor.
            const std::size_t r1 = (column + 1) % 3;
            const std::size_t r2 = (column + 2) % 3;
            const std::size_t c1 = (row + 1) % 3;
            const std::size_t c2 = (row + 2) % 3;
            result[row][column] = ((m[r1][c1] * m[r2][c2]) - (m[r1][c2] * m[r2][c1])) / whole;
        }
    }
    return result;
}

color_components multiply(const matrix& m, const color_components& v) noexcept
{
    color_components result{};
    for (std::size_t row = 0; row < 3; ++row) {
        result[row] = (m[row][0] * v[0]) + (m[row][1] * v[1]) + (m[row][2] * v[2]);
    }
    return result;
}

double clip(double value) noexcept
{
    return std::clamp(value, 0.0, 1.0);
}

color_components clip(const color_components& components) noexcept
{
    return {clip(components[0]), clip(components[1]), clip(components[2])};
}

// Equation [56].
double f(double t) noexcept
{
    return t > epsilon ? std::cbrt(t) : ((kappa * t) + 16.0) / 116.0;
}

// The L component of equation [60], 1.16 f(Y) - 0.16. Its linear part is written as κ Y / 100, the same value, so that
// black gives exactly 0: the rounding residue of the form of the specification, about 1e-17, would become 1e-8 through
// the exponent 1/γ of a gamma working space when the colour goes back to RGB.
double lightness(double y) noexcept
{
    return y > epsilon ? (1.16 * std::cbrt(y)) - 0.16 : kappa * y / 100.0;
}

// Equation [61] for one component of equation [63]: t³, or (116 t - 16) / κ, whose numerator, linear, the caller
// computes from the L*a*b* components so that black gives exactly 0, as above.
double g(double t, double linear) noexcept
{
    const double cube = t * t * t;
    return cube > epsilon ? cube : linear / kappa;
}

bool in_unit_range(const std::array<double, 3>& values) noexcept
{
    return std::ranges::all_of(values, [](double value) { return value >= 0.0 && value <= 1.0; });
}

enum class direction : std::uint8_t
{
    to_lab,
    to_rgb,
};

void convert_pixels(std::span<std::byte> pixels, const image_info& image, direction way)
{
    const std::string what = way == direction::to_lab ? "convert_rgb_to_lab()" : "convert_lab_to_rgb()";
    if (image.color_space != color_space::cie_lab) {
        throw usage_error(errc::invalid_argument, what + " needs the description of a CIE L*a*b* image");
    }
    if (image.geometry.channels < 3) {
        throw usage_error(errc::invalid_argument, what + " needs an image of three channels or more");
    }
    const bounds range = detail::checked_range(image, pixels.size(), what);
    const color_converter converter(image.rgb_working_space.value_or(rgb_working_space{}));
    const std::size_t pixel_count = image.geometry.pixel_count();
    const std::size_t channels = image.geometry.channels;
    detail::visit_real_samples(image.sample_format, [&]<typename T>(std::type_identity<T>) {
        for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
            std::array<std::size_t, 3> index{};
            color_components given{};
            for (std::size_t channel = 0; channel < 3; ++channel) {
                index[channel] = detail::sample_index(image.pixel_storage, pixel, channel, pixel_count, channels);
                given[channel] = detail::normalize(detail::load_value<T>(pixels, index[channel]), range);
            }
            const color_components result =
                way == direction::to_lab ? converter.rgb_to_lab(given) : converter.lab_to_rgb(given);
            for (std::size_t channel = 0; channel < 3; ++channel) {
                detail::store_value<T>(pixels, index[channel], detail::denormalize(result[channel], range));
            }
        }
    });
}

} // namespace

std::optional<std::array<double, 3>> luminance_coefficients(const std::array<double, 3>& x,
                                                            const std::array<double, 3>& y) noexcept
{
    if (std::ranges::any_of(y, [](double value) { return value == 0.0; })) {
        return std::nullopt;
    }
    // Spec §8.5.4.1, equation [3]: the tristimulus values of each primary per unit of luminance, by columns, give the
    // reference white from the luminance coefficients. Cramer's rule solves the system.
    matrix system{};
    for (std::size_t i = 0; i < 3; ++i) {
        system[0][i] = x[i] / y[i];
        system[1][i] = 1.0;
        system[2][i] = (1.0 - x[i] - y[i]) / y[i];
    }
    const double whole = determinant(system);
    if (whole == 0.0 || !std::isfinite(whole)) {
        return std::nullopt;
    }
    constexpr color_components white{d50_x, 1.0, d50_z};
    std::array<double, 3> luminance{};
    for (std::size_t i = 0; i < 3; ++i) {
        matrix replaced = system;
        for (std::size_t row = 0; row < 3; ++row) {
            replaced[row][i] = white[row];
        }
        luminance[i] = determinant(replaced) / whole;
        if (!std::isfinite(luminance[i])) {
            return std::nullopt;
        }
    }
    return luminance;
}

color_components xyz_to_lab(const color_components& xyz) noexcept
{
    // Equation [52] normalizes the tristimulus values to D50, and equation [60] gives the normalized components.
    const double fx = f(xyz[0] / d50_x);
    const double fy = f(xyz[1]);
    const double fz = f(xyz[2] / d50_z);
    return clip({lightness(xyz[1]), 0.5 + (29.0 / 50.0 * (fx - fy)), 0.5 + (29.0 / 50.0 * (fy - fz))});
}

color_components lab_to_xyz(const color_components& lab) noexcept
{
    // Equations [62] and [63], then the tristimulus values of D50. Since 116 / 1.16 = 100 and 116 × 50 / 29 = 200, the
    // values of 116 t - 16 are 100 L and 100 L ± 200 (a - 1/2) or (b - 1/2).
    const color_components given = clip(lab);
    const double l = 100.0 * given[0];
    const double a = given[1] - 0.5;
    const double b = given[2] - 0.5;
    const double fy = (given[0] + 0.16) / 1.16;
    const double fx = fy + (50.0 / 29.0 * a);
    const double fz = fy - (50.0 / 29.0 * b);
    return {d50_x * g(fx, l + (200.0 * a)), g(fy, l), d50_z * g(fz, l - (200.0 * b))};
}

color_converter::color_converter(const rgb_working_space& space)
{
    if (space.gamma && (!std::isfinite(*space.gamma) || *space.gamma <= 0.0)) {
        throw usage_error(errc::invalid_rgb_working_space,
                          "the gamma of an RGB working space is a finite value above zero");
    }
    const std::optional<std::array<double, 3>> luminance = luminance_coefficients(space.x, space.y);
    if (!in_unit_range(space.x) || !in_unit_range(space.y) || !luminance) {
        throw usage_error(
            errc::invalid_rgb_working_space,
            "the chromaticity coordinates of the primaries are not in [0, 1] or define no RGB working space");
    }
    gamma_ = space.gamma.value_or(0.0);
    // Equation [51].
    for (std::size_t i = 0; i < 3; ++i) {
        to_xyz_[0][i] = (*luminance)[i] * space.x[i] / space.y[i];
        to_xyz_[1][i] = (*luminance)[i];
        to_xyz_[2][i] = (*luminance)[i] * (1.0 - space.x[i] - space.y[i]) / space.y[i];
    }
    const double whole = determinant(to_xyz_);
    if (whole == 0.0 || !std::isfinite(whole)) {
        throw usage_error(errc::invalid_rgb_working_space,
                          "the luminance coefficients of the RGB working space make its transformation singular");
    }
    to_rgb_ = inverse(to_xyz_, whole);
}

double color_converter::linearize(double component) const noexcept
{
    const double value = clip(component);
    if (gamma_ == 0.0) {
        // Equation [49].
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    }
    // Equation [48].
    return std::pow(value, gamma_);
}

double color_converter::delinearize(double component) const noexcept
{
    const double value = clip(component);
    if (gamma_ == 0.0) {
        // Equation [55].
        return clip(value <= 0.0031308 ? 12.92 * value : (1.055 * std::pow(value, 1.0 / 2.4)) - 0.055);
    }
    // Equation [54].
    return clip(std::pow(value, 1.0 / gamma_));
}

color_components color_converter::rgb_to_xyz(const color_components& rgb) const noexcept
{
    return multiply(to_xyz_, {linearize(rgb[0]), linearize(rgb[1]), linearize(rgb[2])});
}

color_components color_converter::xyz_to_rgb(const color_components& xyz) const noexcept
{
    // Equation [53]. Clipping the linear components first gives the clipped result, since delinearization is monotonic,
    // and keeps negative values away from the exponent.
    const color_components linear = multiply(to_rgb_, xyz);
    return {delinearize(linear[0]), delinearize(linear[1]), delinearize(linear[2])};
}

color_components color_converter::rgb_to_lab(const color_components& rgb) const noexcept
{
    return xyz_to_lab(rgb_to_xyz(rgb));
}

color_components color_converter::lab_to_rgb(const color_components& lab) const noexcept
{
    return xyz_to_rgb(lab_to_xyz(lab));
}

double color_converter::grayscale(const color_components& rgb) const noexcept
{
    // Equation [64]: the CIE Y component is the second row of M applied to the linear components.
    const color_components linear{linearize(rgb[0]), linearize(rgb[1]), linearize(rgb[2])};
    const double y = (to_xyz_[1][0] * linear[0]) + (to_xyz_[1][1] * linear[1]) + (to_xyz_[1][2] * linear[2]);
    return clip(lightness(y));
}

void convert_rgb_to_lab(std::span<std::byte> pixels, const image_info& image)
{
    convert_pixels(pixels, image, direction::to_lab);
}

void convert_lab_to_rgb(std::span<std::byte> pixels, const image_info& image)
{
    convert_pixels(pixels, image, direction::to_rgb);
}

} // namespace openxisf
