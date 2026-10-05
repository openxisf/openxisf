// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "algorithms/astrometric_model.h"

#include <openxisf/astrometry.h>
#include <openxisf/property.h>

#include "algorithms/celestial_sphere.h"
#include "algorithms/distortion_model.h"
#include "core/quote.h"
#include "model/property_catalog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openxisf::detail {

namespace {

constexpr std::string_view solution_prefix = "AstrometricSolution:";
constexpr std::string_view distortion_prefix = "AstrometricSolution:DistortionModel:";

// The identifier of the property `name` of the namespace.
std::string solution_id(std::string_view name)
{
    return std::string(solution_prefix).append(name);
}

// What makes a layer unavailable: thrown while the layer is read, and caught for that layer.
class unavailable_layer : public std::runtime_error
{
public:
    unavailable_layer(astrometric_status status, const std::string& problem)
        : std::runtime_error(problem), status_(status)
    {}

    [[nodiscard]] astrometric_status status() const noexcept
    {
        return status_;
    }

private:
    astrometric_status status_;
};

[[noreturn]] void fail(astrometric_status status, const std::string& problem)
{
    throw unavailable_layer(status, problem);
}

[[noreturn]] void inconsistent(const std::string& problem)
{
    fail(astrometric_status::inconsistent, problem);
}

std::string count_text(std::uint64_t count)
{
    return std::to_string(count);
}

// -----------------------------------------------------------------------------------------------------------------
// Properties of the types of spec §11.5.3.7, read with their dimensions

// The value of a property of the given type; null when the property is absent and optional.
const property_value* typed(const property_list& properties, const std::string& id, property_type type, bool required)
{
    const property* found = properties.find(id);
    if (found == nullptr) {
        if (required) {
            fail(astrometric_status::missing_property, id + " is missing");
        }
        return nullptr;
    }
    if (found->value.type() != type) {
        inconsistent(id + " has the type " + std::string(property_type_name(found->value.type())) + ", not " +
                     std::string(property_type_name(type)));
    }
    return &found->value;
}

void check_finite(std::span<const double> values, const std::string& id)
{
    if (!std::ranges::all_of(values, [](double value) { return std::isfinite(value); })) {
        inconsistent(id + " holds a value that is not finite");
    }
}

void check_positive(std::span<const double> values, const std::string& id)
{
    if (!std::ranges::all_of(values, [](double value) { return value > 0.0; })) {
        inconsistent(id + " holds a value that is not above zero");
    }
}

std::span<const double> vector_of(const property_value& value, const std::string& id,
                                  std::optional<std::uint64_t> length)
{
    const std::span<const double> elements = value.elements<double>();
    if (length && elements.size() != *length) {
        inconsistent(id + " has " + count_text(elements.size()) + " elements, not " + count_text(*length));
    }
    check_finite(elements, id);
    return elements;
}

std::span<const double> required_vector(const property_list& properties, const std::string& id,
                                        std::optional<std::uint64_t> length)
{
    return vector_of(*typed(properties, id, property_type::f64_vector, true), id, length);
}

std::optional<std::span<const double>> optional_vector(const property_list& properties, const std::string& id,
                                                       std::optional<std::uint64_t> length)
{
    if (const property_value* value = typed(properties, id, property_type::f64_vector, false); value != nullptr) {
        return vector_of(*value, id, length);
    }
    return std::nullopt;
}

std::span<const std::int32_t> required_offsets(const property_list& properties, const std::string& id,
                                               std::uint64_t length)
{
    const std::span<const std::int32_t> elements =
        typed(properties, id, property_type::i32_vector, true)->elements<std::int32_t>();
    if (elements.size() != length) {
        inconsistent(id + " has " + count_text(elements.size()) + " elements, not " + count_text(length));
    }
    return elements;
}

struct matrix_view
{
    std::uint64_t rows = 0;
    std::uint64_t columns = 0;
    std::span<const double> elements{};
};

// A matrix of the given number of columns, and of rows when rows has a value. A matrix without rows may have any number
// of columns.
matrix_view matrix_of(const property_value& value, const std::string& id, std::optional<std::uint64_t> rows,
                      std::uint64_t columns)
{
    const matrix_view result{.rows = value.rows(), .columns = value.columns(), .elements = value.elements<double>()};
    if ((rows && result.rows != *rows) || (result.columns != columns && result.rows != 0)) {
        inconsistent(id + " has " + count_text(result.rows) + " rows and " + count_text(result.columns) +
                     " columns, not " + (rows ? count_text(*rows) : std::string("any number of")) + " rows and " +
                     count_text(columns) + " columns");
    }
    check_finite(result.elements, id);
    return result;
}

matrix_view required_matrix(const property_list& properties, const std::string& id, std::optional<std::uint64_t> rows,
                            std::uint64_t columns)
{
    return matrix_of(*typed(properties, id, property_type::f64_matrix, true), id, rows, columns);
}

std::optional<matrix_view> optional_matrix(const property_list& properties, const std::string& id,
                                           std::optional<std::uint64_t> rows, std::uint64_t columns)
{
    if (const property_value* value = typed(properties, id, property_type::f64_matrix, false); value != nullptr) {
        return matrix_of(*value, id, rows, columns);
    }
    return std::nullopt;
}

double required_float64(const property_list& properties, const std::string& id)
{
    const double value = typed(properties, id, property_type::float64, true)->get<double>();
    check_finite(std::span(&value, 1), id);
    return value;
}

std::optional<double> optional_float64(const property_list& properties, const std::string& id)
{
    if (const property_value* found = typed(properties, id, property_type::float64, false); found != nullptr) {
        const double value = found->get<double>();
        check_finite(std::span(&value, 1), id);
        return value;
    }
    return std::nullopt;
}

const std::string& required_string(const property_list& properties, const std::string& id)
{
    return typed(properties, id, property_type::string, true)->get<std::string>();
}

// Sums and products of counts from a unit, which cannot exceed what a vector holds.
std::uint64_t checked_sum(std::uint64_t a, std::uint64_t b, const std::string& id)
{
    if (b > std::numeric_limits<std::uint64_t>::max() - a) {
        inconsistent(id + " would need more elements than a vector can hold");
    }
    return a + b;
}

std::uint64_t checked_product(std::uint64_t a, std::uint64_t b, const std::string& id)
{
    if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) {
        inconsistent(id + " would need more elements than a vector can hold");
    }
    return a * b;
}

// -----------------------------------------------------------------------------------------------------------------
// The vocabularies (spec §11.5.3.7.2.2, §11.5.3.7.4.1, §11.5.3.7.4.3)

constexpr std::array<projection_system, 7> projection_systems{
    projection_system::gnomonic,     projection_system::stereographic, projection_system::zenithal_equal_area,
    projection_system::orthographic, projection_system::plate_carree,  projection_system::mercator,
    projection_system::hammer_aitoff};

std::optional<projection_system> projection_named(std::string_view name) noexcept
{
    for (const projection_system system : projection_systems) {
        if (projection_system_name(system) == name) {
            return system;
        }
    }
    return std::nullopt;
}

std::optional<basis_function> basis_named(std::string_view name) noexcept
{
    struct named
    {
        std::string_view name;
        basis_function basis;
    };
    constexpr std::array<named, 6> names{{
        {.name = "ThinPlateSpline", .basis = basis_function::thin_plate_spline},
        {.name = "VariableOrder", .basis = basis_function::variable_order},
        {.name = "Gaussian", .basis = basis_function::gaussian},
        {.name = "Multiquadric", .basis = basis_function::multiquadric},
        {.name = "InverseMultiquadric", .basis = basis_function::inverse_multiquadric},
        {.name = "InverseQuadratic", .basis = basis_function::inverse_quadratic},
    }};
    for (const named& entry : names) {
        if (entry.name == name) {
            return entry.basis;
        }
    }
    return std::nullopt;
}

struct term_kinds
{
    bool local = false;
    bool global = false;
    bool fallback = false;
};

// The newline-separated list of the Terms property. Empty lines, such as the end of a list that ends with a newline,
// name no kind.
term_kinds parse_terms(std::string_view text, const std::string& id)
{
    term_kinds kinds;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (line.empty()) {
            continue;
        }
        bool* kind = nullptr;
        if (line == "Local") {
            kind = &kinds.local;
        } else if (line == "Global") {
            kind = &kinds.global;
        } else if (line == "Fallback") {
            kind = &kinds.fallback;
        } else {
            fail(astrometric_status::unknown_identifier,
                 id + " names the kind of term " + quote(line) + ", which is none of the specification");
        }
        if (*kind) {
            inconsistent(id + " names the kind of term " + std::string(line) + " twice");
        }
        *kind = true;
    }
    if (kinds.fallback && !kinds.local) {
        inconsistent(id + " names a Fallback term without Local terms");
    }
    return kinds;
}

// -----------------------------------------------------------------------------------------------------------------
// Layer 1 (spec §11.5.3.7.2)

void read_version(const property_list& properties, astrometric_model& model)
{
    const std::string id = solution_id("Version");
    model.version = required_string(properties, id);
    const std::optional<std::array<std::uint32_t, 2>> revision = parse_astrometric_version(model.version);
    if (!revision) {
        fail(astrometric_status::unsupported_version,
             "the version " + quote(model.version) + " is not of the form major.minor");
    }
    if ((*revision)[0] != 1) {
        fail(astrometric_status::unsupported_version,
             "the solution is of revision " + quote(model.version) + ", and OpenXISF reads the revisions 1.x");
    }
}

std::array<double, 2> pair(std::span<const double> elements)
{
    return {elements[0], elements[1]};
}

void read_projection(const property_list& properties, astrometric_model& model)
{
    const std::string projection_id = solution_id("ProjectionSystem");
    const std::string& name = required_string(properties, projection_id);
    const std::optional<projection_system> system = projection_named(name);
    if (!system) {
        fail(astrometric_status::unknown_identifier,
             "the projection system " + quote(name) + " is none of the specification");
    }
    const std::array<double, 2> reference =
        pair(required_vector(properties, solution_id("ReferenceCelestialCoordinates"), 2));
    if (std::abs(reference[1]) > 90.0) {
        inconsistent("the declination of the reference point is beyond ±90");
    }
    const std::array<double, 2> image = pair(required_vector(properties, solution_id("ReferenceImageCoordinates"), 2));
    const matrix_view linear = required_matrix(properties, solution_id("LinearTransformationMatrix"), 2, 2);

    native_point reference_native = default_reference_native(*system);
    if (const auto given = optional_vector(properties, solution_id("ReferenceNativeCoordinates"), 2)) {
        reference_native = {.phi = (*given)[0], .theta = (*given)[1]};
        if (std::abs(reference_native.theta) > 90.0) {
            inconsistent("the native latitude of the reference point is beyond ±90");
        }
    }
    native_point pole_native = default_pole_native(reference[1], reference_native);
    if (const auto given = optional_vector(properties, solution_id("CelestialPoleNativeCoordinates"), 2)) {
        pole_native = {.phi = (*given)[0], .theta = (*given)[1]};
    }
    const celestial_point celestial{.ra = reference[0], .dec = reference[1]};
    const std::optional<spherical_rotation> rotation =
        spherical_rotation::make(celestial, reference_native, pole_native);
    if (!rotation) {
        inconsistent("the reference point and the native coordinates define no rotation of the sphere (Annex A.2.1)");
    }

    // Layer 1 goes from projection plane coordinates to image coordinates with the inverse of its matrix. A determinant
    // within the rounding error of its products is zero: the matrix is singular, whether the products are rounded
    // apart or fused into one operation, as compilers may do where the processor can.
    const std::span<const double> m = linear.elements;
    const double determinant = (m[0] * m[3]) - (m[1] * m[2]);
    const double products = std::abs(m[0] * m[3]) + std::abs(m[1] * m[2]);
    const std::array<double, 4> inverse{m[3] / determinant, -m[1] / determinant, -m[2] / determinant,
                                        m[0] / determinant};
    if (std::abs(determinant) <= 4.0 * std::numeric_limits<double>::epsilon() * products ||
        !std::ranges::all_of(inverse, [](double value) { return std::isfinite(value); })) {
        inconsistent(solution_id("LinearTransformationMatrix") + " is singular");
    }

    // A reference system of another type than String is unknown; it does not make the layer unavailable.
    std::string reference_system = "ICRS";
    if (const property* found = properties.find(solution_id("CelestialReferenceSystem")); found != nullptr) {
        reference_system = found->value.type() == property_type::string ? found->value.get<std::string>() : "";
    }

    model.projection = astrometric_projection{
        .projection_system = *system,
        .reference_celestial = celestial,
        .reference_image = {.x = image[0], .y = image[1]},
        .linear_transformation = {m[0], m[1], m[2], m[3]},
        .reference_native = {reference_native.phi, reference_native.theta},
        .celestial_pole_native = {pole_native.phi, pole_native.theta},
        .celestial_reference_system = std::move(reference_system),
    };
    model.rotation = *rotation;
    model.inverse_linear = inverse;
}

// -----------------------------------------------------------------------------------------------------------------
// Layer 2 (spec §11.5.3.7.3)

std::array<double, 9> matrix_3x3(const matrix_view& matrix)
{
    std::array<double, 9> result{};
    std::ranges::copy(matrix.elements, result.begin());
    return result;
}

void read_projective(const property_list& properties, astrometric_model& model)
{
    const std::string prefix = solution_id("ProjectiveTransformation:");
    const matrix_view forward = required_matrix(properties, prefix + "ImageToProjection", 3, 3);
    const matrix_view backward = required_matrix(properties, prefix + "ProjectionToImage", 3, 3);
    model.projective = projective_transformations{.image_to_projection = matrix_3x3(forward),
                                                  .projection_to_image = matrix_3x3(backward)};
}

// -----------------------------------------------------------------------------------------------------------------
// Layer 3 (spec §11.5.3.7.4)

// What the splines of a direction share: their basis function, order and polynomial part.
struct spline_kind
{
    basis_function basis = basis_function::thin_plate_spline;
    std::uint64_t polynomial_size = 0;
};

std::array<double, 3> normalization_at(std::span<const double> elements, std::uint64_t row)
{
    const std::size_t first = row * 3;
    return {elements[first], elements[first + 1], elements[first + 2]};
}

double positive_shape(double value, const std::string& id)
{
    if (!(value > 0.0)) {
        inconsistent(id + " is not above zero");
    }
    return value;
}

// The spline record of a Global or Fallback term (spec §11.5.3.7.4.4), whose properties start with prefix. The Y
// component takes each of its nodes, normalization and shape parameter from the X component when it does not specify
// it; Y:Nodes requires Y:Normalization, and Y:ShapeParameter when the basis function has a shape parameter.
distortion_term read_record(const property_list& properties, const std::string& prefix, const spline_kind& kind,
                            term_kind term)
{
    const bool shaped = has_shape_parameter(kind.basis);
    distortion_term result{.kind = term};
    surface_spline& x = result.x;
    x.normalization = normalization_at(required_vector(properties, prefix + "X:Normalization", 3), 0);
    const matrix_view x_nodes = required_matrix(properties, prefix + "X:Nodes", std::nullopt, 2);
    x.nodes.assign(x_nodes.elements.begin(), x_nodes.elements.end());
    const std::string x_coefficients = prefix + "X:Coefficients";
    const std::span<const double> x_values =
        required_vector(properties, x_coefficients, checked_sum(x_nodes.rows, kind.polynomial_size, x_coefficients));
    x.coefficients.assign(x_values.begin(), x_values.end());
    if (shaped) {
        x.shape =
            positive_shape(required_float64(properties, prefix + "X:ShapeParameter"), prefix + "X:ShapeParameter");
    }

    surface_spline& y = result.y;
    const std::optional<matrix_view> y_nodes = optional_matrix(properties, prefix + "Y:Nodes", std::nullopt, 2);
    const std::optional<std::span<const double>> y_normalization =
        optional_vector(properties, prefix + "Y:Normalization", 3);
    std::optional<double> y_shape;
    if (shaped) {
        y_shape = optional_float64(properties, prefix + "Y:ShapeParameter");
        if (y_shape) {
            positive_shape(*y_shape, prefix + "Y:ShapeParameter");
        } else if (y_nodes) {
            fail(astrometric_status::missing_property, prefix + "Y:ShapeParameter is missing, which Y:Nodes requires");
        }
    }
    if (y_nodes && !y_normalization) {
        fail(astrometric_status::missing_property, prefix + "Y:Normalization is missing, which Y:Nodes requires");
    }
    y.normalization = y_normalization ? normalization_at(*y_normalization, 0) : x.normalization;
    y.shape = y_shape.value_or(x.shape);
    const std::uint64_t y_count = y_nodes ? y_nodes->rows : x_nodes.rows;
    const std::string y_coefficients = prefix + "Y:Coefficients";
    const std::span<const double> y_values =
        required_vector(properties, y_coefficients, checked_sum(y_count, kind.polynomial_size, y_coefficients));
    y.coefficients.assign(y_values.begin(), y_values.end());
    result.shared = !y_nodes && !y_normalization && y.shape == x.shape;
    if (y_nodes) {
        y.nodes.assign(y_nodes->elements.begin(), y_nodes->elements.end());
    } else if (!result.shared) {
        y.nodes = x.nodes;
    }
    return result;
}

// Node offsets O0 = 0 ≤ O1 ≤ ... ≤ ON (spec §11.5.3.7.4.4).
std::vector<std::uint64_t> node_offsets(std::span<const std::int32_t> offsets, const std::string& id)
{
    std::vector<std::uint64_t> result;
    result.reserve(offsets.size());
    std::int32_t previous = 0;
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        if ((i == 0 && offsets[i] != 0) || offsets[i] < previous) {
            inconsistent(id + " is not a list of increasing offsets that starts at 0");
        }
        previous = offsets[i];
        result.push_back(static_cast<std::uint64_t>(offsets[i]));
    }
    return result;
}

// The packed splines of one component of the Local terms.
struct packed_splines
{
    matrix_view normalizations{};
    std::vector<std::uint64_t> offsets{};
    matrix_view nodes{};
    std::span<const double> coefficients{};
    std::span<const double> shapes{};
};

surface_spline unpack(const packed_splines& packed, std::uint64_t term, std::uint64_t polynomial_size)
{
    surface_spline spline;
    spline.normalization = normalization_at(packed.normalizations.elements, term);
    const std::uint64_t first = packed.offsets[term];
    const std::uint64_t last = packed.offsets[term + 1];
    spline.nodes.assign(packed.nodes.elements.begin() + static_cast<std::ptrdiff_t>(2 * first),
                        packed.nodes.elements.begin() + static_cast<std::ptrdiff_t>(2 * last));
    const std::uint64_t begin = first + (term * polynomial_size);
    const std::uint64_t end = last + ((term + 1) * polynomial_size);
    spline.coefficients.assign(packed.coefficients.begin() + static_cast<std::ptrdiff_t>(begin),
                               packed.coefficients.begin() + static_cast<std::ptrdiff_t>(end));
    spline.shape = packed.shapes.empty() ? 0.0 : packed.shapes[term];
    return spline;
}

// The Local terms (spec §11.5.3.7.4.4), whose properties start with prefix: N of them, each owning the node rows
// [Oi, Oi+1) and the coefficients [Oi + iQ, Oi+1 + (i + 1)Q) of the packed splines of each component.
void read_locals(const property_list& properties, const std::string& prefix, const spline_kind& kind,
                 std::vector<distortion_term>& terms)
{
    const bool shaped = has_shape_parameter(kind.basis);
    const matrix_view centers = required_matrix(properties, prefix + "Center", std::nullopt, 2);
    const std::uint64_t count = centers.rows;
    const std::span<const double> radii = required_vector(properties, prefix + "Radius", count);
    check_positive(radii, prefix + "Radius");
    const std::uint64_t offset_count = checked_sum(count, 1, prefix + "X:NodeOffsets");
    const std::uint64_t polynomial_total = checked_product(count, kind.polynomial_size, prefix + "X:Coefficients");

    packed_splines x;
    x.normalizations = required_matrix(properties, prefix + "X:Normalization", count, 3);
    x.offsets =
        node_offsets(required_offsets(properties, prefix + "X:NodeOffsets", offset_count), prefix + "X:NodeOffsets");
    x.nodes = required_matrix(properties, prefix + "X:Nodes", x.offsets.back(), 2);
    x.coefficients = required_vector(properties, prefix + "X:Coefficients",
                                     checked_sum(x.offsets.back(), polynomial_total, prefix + "X:Coefficients"));
    if (shaped) {
        x.shapes = required_vector(properties, prefix + "X:ShapeParameter", count);
        check_positive(x.shapes, prefix + "X:ShapeParameter");
    }

    // Each property of the Y components that is not specified is that of the X components.
    const std::optional<matrix_view> y_nodes = optional_matrix(properties, prefix + "Y:Nodes", std::nullopt, 2);
    const std::optional<matrix_view> y_normalizations =
        optional_matrix(properties, prefix + "Y:Normalization", count, 3);
    std::optional<std::vector<std::uint64_t>> y_offsets;
    if (properties.contains(prefix + "Y:NodeOffsets")) {
        y_offsets = node_offsets(required_offsets(properties, prefix + "Y:NodeOffsets", offset_count),
                                 prefix + "Y:NodeOffsets");
    }
    std::optional<std::span<const double>> y_shapes;
    if (shaped) {
        y_shapes = optional_vector(properties, prefix + "Y:ShapeParameter", count);
        if (y_shapes) {
            check_positive(*y_shapes, prefix + "Y:ShapeParameter");
        }
    }
    if (y_nodes) {
        for (const auto& [present, name] :
             {std::pair{y_normalizations.has_value(), "Normalization"}, std::pair{y_offsets.has_value(), "NodeOffsets"},
              std::pair{!shaped || y_shapes.has_value(), "ShapeParameter"}}) {
            if (!present) {
                fail(astrometric_status::missing_property,
                     prefix + "Y:" + name + " is missing, which Y:Nodes requires");
            }
        }
    }
    packed_splines y;
    y.normalizations = y_normalizations.value_or(x.normalizations);
    y.offsets = y_offsets.value_or(x.offsets);
    y.nodes = y_nodes.value_or(x.nodes);
    if (y.nodes.rows != y.offsets.back()) {
        inconsistent(prefix + "Y:NodeOffsets ends at " + count_text(y.offsets.back()) + ", and the nodes have " +
                     count_text(y.nodes.rows) + " rows");
    }
    y.coefficients = required_vector(properties, prefix + "Y:Coefficients",
                                     checked_sum(y.offsets.back(), polynomial_total, prefix + "Y:Coefficients"));
    y.shapes = y_shapes.value_or(x.shapes);
    const bool own_y = y_nodes || y_normalizations || y_offsets;

    for (std::uint64_t i = 0; i < count; ++i) {
        distortion_term term{.kind = term_kind::local,
                             .center = {centers.elements[2 * i], centers.elements[(2 * i) + 1]},
                             .radius = radii[i]};
        term.x = unpack(x, i, kind.polynomial_size);
        term.y = unpack(y, i, kind.polynomial_size);
        term.shared = !own_y && term.y.shape == term.x.shape;
        if (term.shared) {
            term.y.nodes.clear();
        }
        terms.push_back(std::move(term));
    }
}

distortion_model read_direction(const property_list& properties, std::string_view direction)
{
    const std::string prefix = std::string(distortion_prefix).append(direction).append(":");
    distortion_model model;
    const std::string basis_id = prefix + "BasisFunction";
    const std::string& basis_name = required_string(properties, basis_id);
    const std::optional<basis_function> basis = basis_named(basis_name);
    if (!basis) {
        fail(astrometric_status::unknown_identifier,
             "the basis function " + quote(basis_name) + " is none of the specification");
    }
    model.basis = *basis;
    model.order = typed(properties, prefix + "Order", property_type::int32, true)->get<std::int32_t>();
    const std::int32_t minimum = model.basis == basis_function::variable_order      ? 3
                                 : model.basis == basis_function::thin_plate_spline ? 2
                                                                                    : 1;
    if (model.order < minimum) {
        inconsistent(prefix + "Order is " + std::to_string(model.order) + ", below the " + std::to_string(minimum) +
                     " that the basis function " + basis_name + " needs");
    }
    if (const property_value* polynomial = typed(properties, prefix + "Polynomial", property_type::boolean, false);
        polynomial != nullptr) {
        model.polynomial = polynomial->get<bool>();
    }
    if (!model.polynomial &&
        (model.basis == basis_function::thin_plate_spline || model.basis == basis_function::variable_order)) {
        inconsistent(prefix + "Polynomial is false, and the basis function " + basis_name + " needs a polynomial part");
    }
    const term_kinds kinds = parse_terms(required_string(properties, prefix + "Terms"), prefix + "Terms");
    const spline_kind kind{.basis = model.basis,
                           .polynomial_size = model.polynomial ? polynomial_size(model.order) : 0};
    if (kinds.local) {
        read_locals(properties, prefix + "Local:", kind, model.terms);
    }
    if (kinds.global) {
        model.terms.push_back(read_record(properties, prefix + "Global:", kind, term_kind::global));
    }
    if (kinds.fallback) {
        model.threshold =
            positive_shape(required_float64(properties, prefix + "Fallback:Threshold"), prefix + "Fallback:Threshold");
        model.terms.push_back(read_record(properties, prefix + "Fallback:", kind, term_kind::fallback));
    }
    return model;
}

void read_distortion(const property_list& properties, astrometric_model& model)
{
    distortion_model forward = read_direction(properties, "ImageToProjection");
    distortion_model backward = read_direction(properties, "ProjectionToImage");
    model.image_to_projection = std::move(forward);
    model.projection_to_image = std::move(backward);
}

// Reads one layer, and records whether it is available or why not.
template <typename Read> void read_layer(astrometric_model& model, std::size_t index, const Read& read)
{
    // A layer requires the one below it.
    if (index > 0 && model.status[index - 1] != astrometric_status::available) {
        model.status[index] = astrometric_status::lower_layer_unavailable;
        model.problem[index] = "it requires layer " + std::to_string(index) + ", which is not available";
        return;
    }
    try {
        read();
        model.status[index] = astrometric_status::available;
    } catch (const unavailable_layer& failure) {
        model.status[index] = failure.status();
        model.problem[index] = failure.what();
    }
}

// -----------------------------------------------------------------------------------------------------------------
// Evaluation

bool finite(double a, double b) noexcept
{
    return std::isfinite(a) && std::isfinite(b);
}

// The projective transformation of equation [24]: P (a, b, 1), divided by its third component.
std::optional<plane_point> apply_projective(const std::array<double, 9>& m, double a, double b) noexcept
{
    const double u = (m[0] * a) + (m[1] * b) + m[2];
    const double v = (m[3] * a) + (m[4] * b) + m[5];
    const double w = (m[6] * a) + (m[7] * b) + m[8];
    if (w == 0.0) {
        return std::nullopt;
    }
    return plane_point{.u = u / w, .v = v / w};
}

// The image-plane step of a direction through layer 2 or 3: the projective transformation, plus the residual field of
// the distortion model (equation [25]), at a point of the source coordinates.
std::optional<plane_point> image_plane_step(const std::array<double, 9>& projective,
                                            const std::optional<distortion_model>& distortion, bool with_distortion,
                                            double a, double b) noexcept
{
    std::optional<plane_point> result = apply_projective(projective, a, b);
    if (result && with_distortion && distortion) {
        const std::array<double, 2> residual = distortion->residual(a, b);
        result->u += residual[0];
        result->v += residual[1];
    }
    return result;
}

} // namespace

astrometric_model read_astrometric_model(const property_list& properties)
{
    astrometric_model model;
    if (!std::ranges::any_of(properties, [](const property& item) { return is_astrometric_solution_id(item.id); })) {
        return model;
    }
    read_layer(model, 0, [&] {
        read_version(properties, model);
        read_projection(properties, model);
    });
    // A solution of another major revision is not read at all (spec §11.5.3.7.6).
    if (model.status[0] == astrometric_status::unsupported_version) {
        for (std::size_t index = 1; index < model.status.size(); ++index) {
            model.status[index] = astrometric_status::unsupported_version;
            model.problem[index] = model.problem[0];
        }
        return model;
    }
    const std::string projective_prefix = solution_id("ProjectiveTransformation:");
    if (properties.contains(projective_prefix + "ImageToProjection") ||
        properties.contains(projective_prefix + "ProjectionToImage")) {
        read_layer(model, 1, [&] { read_projective(properties, model); });
    }
    if (std::ranges::any_of(properties, [](const property& item) { return item.id.starts_with(distortion_prefix); })) {
        read_layer(model, 2, [&] { read_distortion(properties, model); });
    }
    return model;
}

std::optional<celestial_point> image_to_celestial(const astrometric_model& model, image_point point,
                                                  astrometric_layer layer) noexcept
{
    if (!model.projection || !finite(point.x, point.y)) {
        return std::nullopt;
    }
    const astrometric_projection& projection = *model.projection;
    std::optional<plane_point> plane;
    if (layer == astrometric_layer::linear) {
        // Equation [23].
        const std::array<double, 4>& m = projection.linear_transformation;
        const double dx = point.x - projection.reference_image.x;
        const double dy = point.y - projection.reference_image.y;
        plane = plane_point{.u = (m[0] * dx) + (m[1] * dy), .v = (m[2] * dx) + (m[3] * dy)};
    } else if (model.projective) {
        plane = image_plane_step(model.projective->image_to_projection, model.image_to_projection,
                                 layer == astrometric_layer::distortion, point.x, point.y);
    }
    if (!plane || !finite(plane->u, plane->v)) {
        return std::nullopt;
    }
    const std::optional<native_point> native = deproject(projection.projection_system, *plane);
    if (!native) {
        return std::nullopt;
    }
    const celestial_point result = model.rotation.to_celestial(*native);
    if (!finite(result.ra, result.dec)) {
        return std::nullopt;
    }
    return result;
}

std::optional<image_point> celestial_to_image(const astrometric_model& model, celestial_point point,
                                              astrometric_layer layer) noexcept
{
    if (!model.projection || !finite(point.ra, point.dec) || std::abs(point.dec) > 90.0) {
        return std::nullopt;
    }
    const astrometric_projection& projection = *model.projection;
    const std::optional<plane_point> plane = project(projection.projection_system, model.rotation.to_native(point));
    if (!plane || !finite(plane->u, plane->v)) {
        return std::nullopt;
    }
    std::optional<plane_point> image;
    if (layer == astrometric_layer::linear) {
        // The inverse of equation [23].
        const std::array<double, 4>& m = model.inverse_linear;
        image = plane_point{.u = projection.reference_image.x + (m[0] * plane->u) + (m[1] * plane->v),
                            .v = projection.reference_image.y + (m[2] * plane->u) + (m[3] * plane->v)};
    } else if (model.projective) {
        image = image_plane_step(model.projective->projection_to_image, model.projection_to_image,
                                 layer == astrometric_layer::distortion, plane->u, plane->v);
    }
    if (!image || !finite(image->u, image->v)) {
        return std::nullopt;
    }
    return image_point{.x = image->u, .y = image->v};
}

} // namespace openxisf::detail
