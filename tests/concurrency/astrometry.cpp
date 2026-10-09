// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// An astrometric solution evaluated from 16 threads at once, the solution itself and copies of it, which share its
// data: every thread gets the coordinates of a single thread. ThreadSanitizer runs these tests in CI.

#include <openxisf/astrometry.h>
#include <openxisf/property.h>
#include <openxisf/reader.h>

#include "support/threads.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace {

using openxisf::astrometric_solution;
using openxisf::celestial_point;
using openxisf::image_point;

constexpr std::size_t thread_count = 16;

TEST(concurrency_astrometry, threads_evaluate_one_solution_at_once)
{
    const openxisf::reader unit(std::string(OPENXISF_TEST_DATA_DIR) + "/pixinsight/d5-astrometry-local.xisf");
    const astrometric_solution solved(unit.image(0).properties);
    constexpr int point_count = 64;
    std::vector<image_point> points;
    std::vector<std::optional<celestial_point>> skies;
    std::vector<std::optional<image_point>> images;
    points.reserve(point_count);
    skies.reserve(point_count);
    images.reserve(point_count);
    for (int i = 0; i < point_count; ++i) {
        points.push_back({.x = (i * 37) % 400 + 0.5, .y = (i * 53) % 300 + 0.25});
        const std::optional<celestial_point> sky = solved.image_to_celestial(points.back());
        skies.push_back(sky);
        images.push_back(sky ? solved.celestial_to_image(*sky) : std::nullopt);
    }

    std::atomic<int> failures{0};
    openxisf::test::run_threads(thread_count, [&](std::size_t t) {
        // Every thread copies the solution, and half of them use their copy, which shares its data.
        astrometric_solution copy{openxisf::property_list{}};
        copy = solved;
        const astrometric_solution& used = t % 2 == 0 ? solved : copy;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const std::size_t k = (i + t) % points.size();
            const std::optional<celestial_point> sky = used.image_to_celestial(points[k]);
            if (sky != skies[k] || (sky && used.celestial_to_image(*sky) != images[k])) {
                failures.fetch_add(1);
            }
        }
    });
    EXPECT_EQ(failures.load(), 0);
}

} // namespace
