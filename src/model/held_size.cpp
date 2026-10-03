// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "model/held_size.h"

#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace openxisf::detail {

namespace {

std::uint64_t value_size(const property_value& value)
{
    return std::visit(
        []<typename T>(const T& held) -> std::uint64_t {
            if constexpr (std::is_same_v<T, std::string>) {
                return held.size();
            } else if constexpr (requires { held.size(); }) {
                return held.size() * sizeof(typename T::value_type);
            } else {
                return 0;
            }
        },
        value.data());
}

std::uint64_t format_size(const std::optional<property_format>& format)
{
    return format ? format->unit.size() : 0;
}

template <typename Item> std::uint64_t sum_of(const Item& items)
{
    std::uint64_t total = 0;
    for (const auto& item : items) {
        total += held_size(item);
    }
    return total;
}

// What an Image and a Thumbnail element can both have.
template <typename Image> std::uint64_t common_size(const Image& image)
{
    std::uint64_t total = image.id.size() + image.uuid.size() +
                          (image.geometry.dimensions.size() * sizeof(std::uint64_t)) + sum_of(image.properties) +
                          sum_of(image.tables) + sum_of(image.fits_keywords) + held_size(image.icc_profile);
    if (image.rgb_working_space) {
        total += held_size(*image.rgb_working_space);
    }
    if (image.display_function) {
        total += held_size(*image.display_function);
    }
    return total;
}

} // namespace

std::uint64_t held_size(const property& item)
{
    return item.id.size() + item.comment.size() + format_size(item.format) + value_size(item.value);
}

std::uint64_t held_size(const table& item)
{
    std::uint64_t total = item.id.size() + item.caption.size() + item.comment.size();
    for (const table_field& field : item.fields) {
        total += field.id.size() + field.header.size() + format_size(field.format);
    }
    for (const std::vector<property_value>& row : item.rows) {
        total += row.size() * sizeof(property_value);
        for (const property_value& cell : row) {
            total += value_size(cell);
        }
    }
    return total;
}

std::uint64_t held_size(const fits_keyword& item)
{
    return item.name.size() + item.value.size() + item.comment.size();
}

std::uint64_t held_size(const std::vector<std::byte>& bytes)
{
    return bytes.size();
}

std::uint64_t held_size(const rgb_working_space& item)
{
    return item.name.size();
}

std::uint64_t held_size(const display_function& item)
{
    return item.name.size();
}

std::uint64_t held_size(const color_filter_array& item)
{
    return item.pattern.size() + item.name.size();
}

std::uint64_t held_size(const resolution& /*item*/)
{
    return 0;
}

std::uint64_t held_size(const thumbnail& item)
{
    return common_size(item) + item.pixels.size();
}

std::uint64_t held_size(const image_info& item)
{
    std::uint64_t total = common_size(item);
    if (item.color_filter_array) {
        total += held_size(*item.color_filter_array);
    }
    if (item.thumbnail) {
        total += held_size(*item.thumbnail);
    }
    return total;
}

} // namespace openxisf::detail
