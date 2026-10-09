// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

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

// The elements of items, each with what it holds.
template <typename Items> std::uint64_t sum_of(const Items& items)
{
    std::uint64_t total = 0;
    for (const auto& item : items) {
        total += held_size(item);
    }
    return total;
}

// What an Image and a Thumbnail element can both have. An optional member takes its room in the object whether it
// holds a value or not, so only what that value holds is added.
template <typename Image> std::uint64_t common_size(const Image& image)
{
    std::uint64_t total = image.id.size() + image.uuid.size() +
                          (image.geometry.dimensions.size() * sizeof(std::uint64_t)) + sum_of(image.properties) +
                          sum_of(image.tables) + sum_of(image.fits_keywords) + heap_size(image.icc_profile);
    if (image.rgb_working_space) {
        total += heap_size(*image.rgb_working_space);
    }
    if (image.display_function) {
        total += heap_size(*image.display_function);
    }
    return total;
}

} // namespace

std::uint64_t heap_size(const property& item)
{
    return item.id.size() + item.comment.size() + format_size(item.format) + value_size(item.value);
}

std::uint64_t heap_size(const table& item)
{
    std::uint64_t total = item.id.size() + item.caption.size() + item.comment.size();
    for (const table_field& field : item.fields) {
        total += sizeof(table_field) + field.id.size() + field.header.size() + format_size(field.format);
    }
    for (const std::vector<property_value>& row : item.rows) {
        total += sizeof(std::vector<property_value>) + (row.size() * sizeof(property_value));
        for (const property_value& cell : row) {
            total += value_size(cell);
        }
    }
    return total;
}

std::uint64_t heap_size(const fits_keyword& item)
{
    return item.name.size() + item.value.size() + item.comment.size();
}

std::uint64_t heap_size(const std::vector<std::byte>& bytes)
{
    return bytes.size();
}

std::uint64_t heap_size(const rgb_working_space& item)
{
    return item.name.size();
}

std::uint64_t heap_size(const display_function& item)
{
    return item.name.size();
}

std::uint64_t heap_size(const color_filter_array& item)
{
    return item.pattern.size() + item.name.size();
}

std::uint64_t heap_size(const resolution& /*item*/)
{
    return 0;
}

std::uint64_t heap_size(const thumbnail& item)
{
    return common_size(item) + item.pixels.size();
}

std::uint64_t heap_size(const image_info& item)
{
    std::uint64_t total = common_size(item);
    if (item.color_filter_array) {
        total += heap_size(*item.color_filter_array);
    }
    if (item.thumbnail) {
        total += heap_size(*item.thumbnail);
    }
    return total;
}

} // namespace openxisf::detail
