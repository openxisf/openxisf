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

TEST(held_size, a_property_takes_its_size_its_texts_and_elements)
{
    // The identifier, the comment, the unit and the elements: 6 + 1 + 1 + 3 x 2 bytes.
    const openxisf::property item{.id = "Test:V",
                                  .value = property_value(std::vector<std::uint16_t>{1, 2, 3}),
                                  .format = openxisf::property_format{.unit = "m"},
                                  .comment = "c"};
    constexpr std::uint64_t object = sizeof(openxisf::property);
    EXPECT_EQ(held_size(item), object + 14U);
    EXPECT_EQ(held_size(openxisf::property{.id = "S", .value = property_value("abc")}), object + 4U);
    EXPECT_EQ(held_size(openxisf::property{.id = "I", .value = property_value(std::int32_t{7})}), object + 1U);
}

TEST(held_size, a_table_takes_its_size_its_texts_fields_rows_and_cells)
{
    // The texts: 1 + 2 + 3; the field: its size, and 2 + 6 + 2; the rows and cells: their size each, and the bytes of
    // their values.
    const openxisf::table item{.id = "T",
                               .fields = {{.id = "id",
                                           .type = openxisf::property_type::string,
                                           .header = "header",
                                           .format = openxisf::property_format{.unit = "km"}}},
                               .rows = {{property_value("abcd")}, {property_value("")}},
                               .caption = "ca",
                               .comment = "com"};
    EXPECT_EQ(held_size(item), sizeof(openxisf::table) + 6U + (sizeof(openxisf::table_field) + 10U) +
                                   (2 * (sizeof(std::vector<property_value>) + sizeof(property_value))) + 4U);
}

TEST(held_size, the_elements_that_describe_an_image_take_their_size_and_texts)
{
    EXPECT_EQ(held_size(openxisf::fits_keyword{.name = "OBJECT", .value = "'M31'", .comment = "x"}),
              sizeof(openxisf::fits_keyword) + 12U);
    EXPECT_EQ(held_size(std::vector<std::byte>(3024)), sizeof(std::vector<std::byte>) + 3024U);
    EXPECT_EQ(held_size(openxisf::rgb_working_space{.name = "Adobe RGB (1998)"}),
              sizeof(openxisf::rgb_working_space) + 16U);
    EXPECT_EQ(held_size(openxisf::display_function{.name = "STF"}), sizeof(openxisf::display_function) + 3U);
    EXPECT_EQ(held_size(openxisf::color_filter_array{.pattern = "RGGB", .width = 2, .height = 2, .name = "Bayer"}),
              sizeof(openxisf::color_filter_array) + 9U);
    EXPECT_EQ(held_size(openxisf::resolution{}), sizeof(openxisf::resolution));
}

TEST(held_size, an_image_takes_its_size_what_it_has_and_its_thumbnail)
{
    // Member by member: GCC 16 at -O3 warns that a thumbnail made with nested designated initializers may be used
    // uninitialized when it is copied.
    openxisf::thumbnail small;
    small.geometry.dimensions = {2, 2};
    small.id = "t";
    small.fits_keywords.push_back({.name = "A", .value = "1", .comment = ""});
    small.pixels.resize(4);
    // The identifier, two dimensions, the keyword and the pixels.
    EXPECT_EQ(held_size(small), sizeof(openxisf::thumbnail) + 1U + (2 * sizeof(std::uint64_t)) +
                                    (sizeof(openxisf::fits_keyword) + 2U) + 4U);

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
    // The identifiers, three dimensions, the property, the table and its field, the ICC profile, the names, the
    // pattern and the thumbnail. The elements and the thumbnail held in optional members take room in the image, which
    // is counted once.
    EXPECT_EQ(held_size(image), sizeof(openxisf::image_info) + 5U + 36U + (3 * sizeof(std::uint64_t)) +
                                    (sizeof(openxisf::property) + 6U) +
                                    (sizeof(openxisf::table) + sizeof(openxisf::table_field) + 2U) + 10U + 2U + 2U +
                                    4U + (held_size(small) - sizeof(openxisf::thumbnail)));
}

} // namespace
