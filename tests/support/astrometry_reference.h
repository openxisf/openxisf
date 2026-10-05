// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/astrometry.h>
#include <openxisf/io.h>
#include <openxisf/property.h>

#include "core/text_grammar.h"
#include "support/bytes.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Reference coordinates of astrometric solutions, written by independent implementations: lines of comma-separated
// fields, after comments that start with #.
//
//   solution,<name>,<projection>,<ra0>,<dec0>,<m00>,<m01>,<m10>,<m11>,<x0>,<y0>[,<phi0>,<theta0>,<phip>,<thetap>]
//   invalid,<name>,<the fields of a solution>     parameters that define no solution
//   image,<name>,<x>,<y>,<ra>,<dec>               empty coordinates: the point has none
//   celestial,<name>,<ra>,<dec>,<x>,<y>
//
// A solution line describes the first layer of a solution; the name of the others is that of the unit that holds it.

namespace openxisf::test {

struct reference_solution
{
    std::string name{};
    bool valid = true;
    property_list properties{};
    /// Degrees per pixel: the square root of the determinant of the linear transformation.
    double scale = 0.0;
};

struct reference_point
{
    /// "image" for image to celestial coordinates, "celestial" for the opposite.
    std::string direction{};
    std::string name{};
    std::array<double, 2> input{};
    std::optional<std::array<double, 2>> output{};
};

struct reference_data
{
    std::vector<reference_solution> solutions{};
    std::vector<reference_point> points{};
};

inline std::vector<std::string_view> split_fields(std::string_view line)
{
    std::vector<std::string_view> fields;
    while (true) {
        const std::size_t comma = line.find(',');
        fields.push_back(line.substr(0, comma));
        if (comma == std::string_view::npos) {
            return fields;
        }
        line = line.substr(comma + 1);
    }
}

inline double number(std::string_view field)
{
    return detail::parse_float<double>(field);
}

inline reference_solution make_reference_solution(const std::vector<std::string_view>& fields)
{
    const std::string prefix = "AstrometricSolution:";
    reference_solution result;
    result.name = std::string(fields[1]);
    result.valid = fields[0] == "solution";
    property_list& properties = result.properties;
    properties.set(prefix + "Version", "1.0");
    properties.set(prefix + "ProjectionSystem", std::string(fields[2]));
    properties.set(prefix + "ReferenceCelestialCoordinates", std::vector<double>{number(fields[3]), number(fields[4])});
    const std::vector<double> matrix{number(fields[5]), number(fields[6]), number(fields[7]), number(fields[8])};
    properties.set(prefix + "LinearTransformationMatrix", property_value::matrix(2, 2, matrix));
    properties.set(prefix + "ReferenceImageCoordinates", std::vector<double>{number(fields[9]), number(fields[10])});
    if (fields.size() > 12 && !fields[11].empty()) {
        properties.set(prefix + "ReferenceNativeCoordinates",
                       std::vector<double>{number(fields[11]), number(fields[12])});
    }
    if (fields.size() > 14 && !fields[13].empty()) {
        properties.set(prefix + "CelestialPoleNativeCoordinates",
                       std::vector<double>{number(fields[13]), number(fields[14])});
    }
    result.scale = std::sqrt(std::abs((matrix[0] * matrix[3]) - (matrix[1] * matrix[2])));
    return result;
}

/// The reference data of the file at path, under the directory of the test data.
inline reference_data read_reference_data(const std::string& path)
{
    const file_source source(std::string(OPENXISF_TEST_DATA_DIR) + "/" + path);
    std::vector<std::byte> bytes(source.size());
    source.read(0, bytes);
    const std::string content = text(bytes);
    reference_data result;
    std::string_view rest = content;
    while (!rest.empty()) {
        const std::size_t end = rest.find('\n');
        const std::string_view line = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::vector<std::string_view> fields = split_fields(line);
        if (fields[0] == "solution" || fields[0] == "invalid") {
            result.solutions.push_back(make_reference_solution(fields));
            continue;
        }
        reference_point point{.direction = std::string(fields[0]),
                              .name = std::string(fields[1]),
                              .input = {number(fields[2]), number(fields[3])},
                              .output = std::nullopt};
        if (!fields[4].empty()) {
            point.output = std::array<double, 2>{number(fields[4]), number(fields[5])};
        }
        result.points.push_back(point);
    }
    return result;
}

/// The angular distance between two points of the sky, in degrees, by the haversine formula, which keeps its precision
/// for small distances.
inline double separation(celestial_point a, celestial_point b)
{
    const double rad = std::numbers::pi / 180.0;
    const double dec = std::sin((b.dec - a.dec) * rad / 2.0);
    const double ra = std::sin((b.ra - a.ra) * rad / 2.0);
    const double h = (dec * dec) + (std::cos(a.dec * rad) * std::cos(b.dec * rad) * ra * ra);
    return 2.0 * std::asin(std::sqrt(h)) / rad;
}

} // namespace openxisf::test
