// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "model/held_size.h"

#include <openxisf/image.h>
#include <openxisf/property.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using openxisf::property_value;
using openxisf::detail::held_size;

TEST(held_size, a_property_holds_its_texts_and_elements)
{
    // The identifier, the comment, the unit and the elements: 6 + 1 + 1 + 3 x 2 bytes.
    const openxisf::property item{.id = "Test:V",
                                  .value = property_value(std::vector<std::uint16_t>{1, 2, 3}),
                                  .format = openxisf::property_format{.unit = "m"},
                                  .comment = "c"};
    EXPECT_EQ(held_size(item), 14U);
    EXPECT_EQ(held_size(openxisf::property{.id = "S", .value = property_value("abc")}), 4U);
    EXPECT_EQ(held_size(openxisf::property{.id = "I", .value = property_value(std::int32_t{7})}), 1U);
}

TEST(held_size, a_table_holds_its_texts_fields_and_cells)
{
    // The texts: 1 + 2 + 3; the field: 2 + 6 + 2; the cells: their size each, and the bytes of their values.
    const openxisf::table item{.id = "T",
                               .fields = {{.id = "id",
                                           .type = openxisf::property_type::string,
                                           .header = "header",
                                           .format = openxisf::property_format{.unit = "km"}}},
                               .rows = {{property_value("abcd")}, {property_value("")}},
                               .caption = "ca",
                               .comment = "com"};
    EXPECT_EQ(held_size(item), 6U + 10U + (2 * sizeof(property_value)) + 4U);
}

TEST(held_size, the_elements_that_describe_an_image_hold_their_texts)
{
    EXPECT_EQ(held_size(openxisf::fits_keyword{.name = "OBJECT", .value = "'M31'", .comment = "x"}), 12U);
    EXPECT_EQ(held_size(std::vector<std::byte>(3024)), 3024U);
    EXPECT_EQ(held_size(openxisf::rgb_working_space{.name = "Adobe RGB (1998)"}), 16U);
    EXPECT_EQ(held_size(openxisf::display_function{.name = "STF"}), 3U);
    EXPECT_EQ(held_size(openxisf::color_filter_array{.pattern = "RGGB", .width = 2, .height = 2, .name = "Bayer"}), 9U);
    EXPECT_EQ(held_size(openxisf::resolution{}), 0U);
}

TEST(held_size, an_image_holds_what_it_has_and_its_thumbnail)
{
    // Member by member: GCC 16 at -O3 warns that a thumbnail made with nested designated initializers may be used
    // uninitialized when it is copied.
    openxisf::thumbnail small;
    small.geometry.dimensions = {2, 2};
    small.id = "t";
    small.fits_keywords.push_back({.name = "A", .value = "1", .comment = ""});
    small.pixels.resize(4);
    // The identifier, two dimensions, the keyword and the pixels.
    EXPECT_EQ(held_size(small), 1U + (2 * sizeof(std::uint64_t)) + 2U + 4U);

    openxisf::image_info image;
    image.geometry = {.dimensions = {4, 4, 1}, .channels = 3};
    image.id = "image";
    image.uuid = "c5c93b6d-9072-4e85-9548-1a5391377683";
    image.properties.set("P", property_value("value"));
    image.tables.push_back({.id = "T", .fields = {{.id = "f"}}});
    image.icc_profile.resize(10);
    image.rgb_working_space = openxisf::rgb_working_space{.name = "ws"};
    image.display_function = openxisf::display_function{.name = "df"};
    image.color_filter_array = openxisf::color_filter_array{.pattern = "RGGB", .width = 2, .height = 2};
    image.thumbnail = small;
    // The identifiers, three dimensions, the property, the table, the ICC profile, the names, the pattern and the
    // thumbnail.
    EXPECT_EQ(held_size(image),
              5U + 36U + (3 * sizeof(std::uint64_t)) + 6U + 2U + 10U + 2U + 2U + 4U + held_size(small));
}

} // namespace
