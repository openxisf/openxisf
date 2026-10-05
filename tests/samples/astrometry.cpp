// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The samples of astrometric solutions (spec §11.5.3.7), written by PixInsight: the solutions of D5, D5L and D5M, read
// with every layer and evaluated as PixInsight evaluates them, and the linear solutions of every projection.

#include <openxisf/astrometry.h>
#include <openxisf/property.h>
#include <openxisf/reader.h>

#include "samples/sample_catalog.h"
#include "support/astrometry_reference.h"
#include "support/opened_unit.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using openxisf::astrometric_layer;
using openxisf::astrometric_solution;
using openxisf::celestial_point;
using openxisf::image_point;
using openxisf::test::reference_data;
using openxisf::test::reference_point;
using openxisf::test::separation;
using openxisf::test::value_of;

// Spec §11.5.3.7: two conforming implementations agree within 1e-6 pixels.
constexpr double tolerance = 1e-6;

openxisf::property_list properties_of(std::string_view file)
{
    const openxisf::reader unit(std::string(OPENXISF_TEST_DATA_DIR) + "/pixinsight/" + std::string(file),
                                {.strict = true});
    return unit.image(0).properties;
}

double scale_of(const astrometric_solution& solved)
{
    const std::array<double, 4>& m = value_of(solved.projection()).linear_transformation;
    return std::sqrt(std::abs((m[0] * m[3]) - (m[1] * m[2])));
}

// The largest differences, in pixels, between the solutions and the reference coordinates of points: on the sky
// through the scale of each solution, and in the image.
std::size_t compare(const std::map<std::string, astrometric_solution>& solutions,
                    const std::vector<reference_point>& points, double limit)
{
    std::size_t compared = 0;
    for (const reference_point& point : points) {
        SCOPED_TRACE(point.name + " " + point.direction + " " + std::to_string(point.input[0]) + " " +
                     std::to_string(point.input[1]));
        const astrometric_solution& solved = solutions.at(point.name);
        if (point.direction == "image") {
            const std::optional<celestial_point> sky =
                solved.image_to_celestial({.x = point.input[0], .y = point.input[1]});
            EXPECT_EQ(sky.has_value(), point.output.has_value());
            if (sky && point.output) {
                const double pixels =
                    separation(*sky, {.ra = (*point.output)[0], .dec = (*point.output)[1]}) / scale_of(solved);
                EXPECT_LT(pixels, limit);
                ++compared;
            }
        } else {
            const std::optional<image_point> image =
                solved.celestial_to_image({.ra = point.input[0], .dec = point.input[1]});
            EXPECT_EQ(image.has_value(), point.output.has_value());
            if (image && point.output) {
                EXPECT_LT(std::hypot(image->x - (*point.output)[0], image->y - (*point.output)[1]), limit);
                ++compared;
            }
        }
    }
    return compared;
}

TEST(samples_astrometry, pixinsight_writes_every_layer_of_its_solutions)
{
    struct expectation
    {
        std::string_view id;
        std::string_view terms;
        std::string_view basis;
    };
    for (const expectation& sample : {expectation{.id = "D5", .terms = "Global", .basis = "ThinPlateSpline"},
                                      expectation{.id = "D5L", .terms = "Local\nFallback", .basis = "VariableOrder"},
                                      expectation{.id = "D5M", .terms = "Global", .basis = "Multiquadric"}}) {
        SCOPED_TRACE(sample.id);
        const openxisf::property_list properties = properties_of(openxisf::test::sample_by_id(sample.id).file);
        const astrometric_solution solved(properties);
        EXPECT_EQ(solved.layer(), astrometric_layer::distortion) << solved.problem(astrometric_layer::distortion);
        EXPECT_EQ(solved.version(), "1.0");
        const openxisf::astrometric_projection& projection = value_of(solved.projection());
        EXPECT_EQ(projection.projection_system, openxisf::projection_system::gnomonic);
        EXPECT_EQ(projection.reference_celestial, (celestial_point{.ra = 83.82208, .dec = -5.39111}));
        EXPECT_EQ(projection.reference_native, (std::array<double, 2>{0.0, 90.0}));
        EXPECT_EQ(projection.celestial_pole_native, (std::array<double, 2>{180.0, 90.0}));
        EXPECT_EQ(projection.celestial_reference_system, "ICRS");
        for (const std::string_view direction : {"ImageToProjection", "ProjectionToImage"}) {
            const std::string prefix = "AstrometricSolution:DistortionModel:" + std::string(direction) + ":";
            EXPECT_EQ(properties.at(prefix + "Terms").value, openxisf::property_value(sample.terms));
            EXPECT_EQ(properties.at(prefix + "BasisFunction").value, openxisf::property_value(sample.basis));
        }
    }
}

TEST(samples_astrometry, the_solutions_agree_with_pixinsight_within_the_tolerance_of_the_specification)
{
    // tests/data/pixinsight/d5-reference.csv, PixInsight's own coordinates for the corners, the centre, scattered
    // points and points beyond the image, and for sky positions, evaluated exactly (make_group_d5.js).
    const reference_data data = openxisf::test::read_reference_data("pixinsight/d5-reference.csv");
    std::map<std::string, astrometric_solution> solutions;
    for (const reference_point& point : data.points) {
        if (!solutions.contains(point.name)) {
            solutions.emplace(point.name, astrometric_solution(properties_of(point.name)));
        }
    }
    EXPECT_EQ(solutions.size(), 3U);
    EXPECT_EQ(compare(solutions, data.points, tolerance), 3U * (28U + 5U));
}

TEST(samples_astrometry, linear_solutions_of_every_projection_agree_with_pixinsight)
{
    // tests/data/pixinsight/d5-projections.csv: the first layer alone, with the default native coordinates, for the
    // seven projections about eight reference points, the celestial poles among them.
    const reference_data data = openxisf::test::read_reference_data("pixinsight/d5-projections.csv");
    std::map<std::string, astrometric_solution> solutions;
    for (openxisf::test::reference_solution reference : data.solutions) {
        if (reference.name == "Gnomonic-6") {
            // PixInsight evaluates Gnomonic solutions with the direct formulas of Annex A.3 (equations [39] and [40]),
            // which take φp = 180, also with the reference point at the celestial pole, where the default of Annex A.2
            // is 0.
            reference.properties.set("AstrometricSolution:CelestialPoleNativeCoordinates",
                                     std::vector<double>{180.0, 90.0});
        }
        solutions.emplace(reference.name, astrometric_solution(reference.properties));
    }
    EXPECT_EQ(solutions.size(), 56U);
    EXPECT_GT(compare(solutions, data.points, 1e-8), 700U);
}

} // namespace
