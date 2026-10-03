// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

// Lists what a unit holds: how it is stored, its metadata, its standalone properties and tables, its images with their
// attributes, properties, tables, FITS keywords and the elements that describe them, and the problems the reader found
// in it. The path is UTF-8 on every platform; on Windows the program
// takes its arguments from the wide command line (arguments.h), so that a file name of any characters reaches the
// library intact. With --header-only, the reader reads the header alone: the ICC profiles, the pixels of thumbnails and
// the values in data blocks are not loaded.
//
//   read_info [--header-only] <file.xisf>

#include <openxisf/openxisf.h>

#include "arguments.h"

#include <array>
#include <charconv>
#include <complex>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

// The shortest text that reads back as value.
template <typename T> std::string number(T value)
{
    if constexpr (std::is_floating_point_v<T>) {
        std::array<char, 32> text{};
        const std::to_chars_result result = std::to_chars(text.data(), text.data() + text.size(), value);
        return {text.data(), result.ptr};
    } else {
        return std::to_string(value);
    }
}

std::string two_digits(unsigned int value)
{
    return (value < 10 ? "0" : "") + std::to_string(value);
}

// A value, briefly: scalars and strings as they are (long strings cut), vectors and matrices by their size.
std::string describe(const openxisf::property_value& value)
{
    return std::visit(
        [&value]<typename T>(const T& held) -> std::string {
            if constexpr (std::is_same_v<T, std::string>) {
                // Float128 and Complex128 scalars are held as their text.
                if (value.type() != openxisf::property_type::string) {
                    return held;
                }
                return '"' + (held.size() <= 60 ? held : held.substr(0, 57) + "...") + '"';
            } else if constexpr (std::is_same_v<T, bool>) {
                return held ? "true" : "false";
            } else if constexpr (std::is_arithmetic_v<T>) {
                return number(held);
            } else if constexpr (std::is_same_v<T, std::complex<float>> || std::is_same_v<T, std::complex<double>>) {
                return "(" + number(held.real()) + ", " + number(held.imag()) + ")";
            } else if constexpr (std::is_same_v<T, openxisf::date_time>) {
                return std::to_string(held.year) + "-" + two_digits(held.month) + "-" + two_digits(held.day) + "T" +
                       two_digits(held.hour) + ":" + two_digits(held.minute) + ":" + two_digits(held.second) + "Z";
            } else if constexpr (std::is_same_v<T, openxisf::int128> || std::is_same_v<T, openxisf::uint128>) {
                return "a 128-bit integer";
            } else if (value.rows() != 0 || value.columns() != 0) {
                return std::to_string(value.rows()) + " x " + std::to_string(value.columns()) + " matrix";
            } else {
                return std::to_string(value.length()) + " elements";
            }
        },
        value.data());
}

void list_properties(const openxisf::property_list& properties, std::string_view indent)
{
    for (const openxisf::property& item : properties) {
        std::cout << indent << item.id << " (" << openxisf::property_type_name(item.value.type())
                  << "): " << describe(item.value) << '\n';
    }
}

void list_tables(std::span<const openxisf::table> tables, std::string_view indent)
{
    for (const openxisf::table& item : tables) {
        std::cout << indent << item.id << " (table): " << item.rows.size() << " rows of";
        for (const openxisf::table_field& field : item.fields) {
            std::cout << ' ' << (field.header.empty() ? field.id : field.header);
        }
        std::cout << (item.caption.empty() ? "" : ", '" + item.caption + "'") << '\n';
    }
}

std::string triplet(const std::array<double, 3>& values)
{
    return number(values[0]) + ":" + number(values[1]) + ":" + number(values[2]);
}

// The elements that describe an image (spec §11.6 to §11.12), each when the unit has it.
void list_description(const openxisf::image_info& info)
{
    for (const openxisf::fits_keyword& keyword : info.fits_keywords) {
        std::cout << "  " << keyword.name << " = " << keyword.value
                  << (keyword.comment.empty() ? "" : " / " + keyword.comment) << '\n';
    }
    if (!info.icc_profile.empty()) {
        std::cout << "  ICC profile of " << info.icc_profile.size() << " bytes\n";
    }
    if (const std::optional<openxisf::rgb_working_space>& space = info.rgb_working_space) {
        std::cout << "  RGB working space " << (space->name.empty() ? "" : "'" + space->name + "', ") << "gamma "
                  << (space->gamma ? number(*space->gamma) : "sRGB") << ", x " << triplet(space->x) << ", y "
                  << triplet(space->y) << ", Y " << triplet(space->luminance) << '\n';
    }
    if (const std::optional<openxisf::display_function>& function = info.display_function) {
        std::cout << "  display function, midtones";
        for (const double m : function->midtones) {
            std::cout << ' ' << number(m);
        }
        std::cout << '\n';
    }
    if (const std::optional<openxisf::color_filter_array>& filter = info.color_filter_array) {
        std::cout << "  colour filter array " << filter->pattern << " (" << filter->width << " x " << filter->height
                  << ")" << (filter->name.empty() ? "" : ", '" + filter->name + "'") << '\n';
    }
    if (const std::optional<openxisf::resolution>& density = info.resolution) {
        std::cout << "  " << number(density->horizontal) << " x " << number(density->vertical) << " pixels per "
                  << openxisf::resolution_unit_name(density->unit) << '\n';
    }
    if (const std::optional<openxisf::thumbnail>& small = info.thumbnail) {
        std::cout << "  thumbnail of " << small->geometry.dimensions[0] << " x " << small->geometry.dimensions[1]
                  << " pixels, " << openxisf::color_space_name(small->color_space) << ", "
                  << openxisf::sample_format_name(small->sample_format) << ", " << small->pixels.size() << " bytes\n";
    }
}

void list_image(const openxisf::image_info& info)
{
    std::cout << "  ";
    for (std::size_t i = 0; i < info.geometry.dimensions.size(); ++i) {
        std::cout << (i == 0 ? "" : " x ") << info.geometry.dimensions[i];
    }
    std::cout << " pixels, " << info.geometry.channels
              << (info.geometry.channels == 1 ? " channel of " : " channels of ")
              << openxisf::sample_format_name(info.sample_format) << " samples, "
              << openxisf::color_space_name(info.color_space) << ", "
              << openxisf::pixel_storage_name(info.pixel_storage) << " storage, " << info.data_size() << " bytes\n";
    if (const std::optional<openxisf::bounds> range = info.representable_range()) {
        std::cout << "  representable range " << number(range->lower) << " to " << number(range->upper) << '\n';
    }
    if (info.image_type) {
        std::cout << "  image type " << openxisf::image_type_name(*info.image_type) << '\n';
    }
    if (info.offset != 0.0) {
        std::cout << "  offset " << number(info.offset) << '\n';
    }
    if (info.orientation != openxisf::orientation::none) {
        std::cout << "  orientation " << openxisf::orientation_name(info.orientation) << '\n';
    }
    if (!info.uuid.empty()) {
        std::cout << "  uuid " << info.uuid << '\n';
    }
    list_properties(info.properties, "  ");
    list_tables(info.tables, "  ");
    list_description(info);
}

std::string_view severity_name(openxisf::severity level)
{
    switch (level) {
    case openxisf::severity::info:
        return "info";
    case openxisf::severity::warning:
        return "warning";
    case openxisf::severity::error:
        return "error";
    }
    return {};
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const std::vector<std::string> arguments = example::utf8_arguments(argc, argv);
        const bool header_only = arguments.size() == 3 && arguments[1] == "--header-only";
        if (arguments.size() != 2 && !header_only) {
            std::cerr << "usage: read_info [--header-only] <file.xisf>\n";
            return 2;
        }
        // Without the ancillary data, file.load_ancillary_data() would load them later.
        const openxisf::reader file(arguments.back(), {.header_only = header_only});

        std::cout << (file.storage() == openxisf::unit_storage::monolithic ? "monolithic file" : "header file")
                  << (file.signature() == openxisf::signature_status::none ? "" : ", signed (not verified)") << '\n';
        std::cout << "metadata:\n";
        list_properties(file.metadata(), "  ");
        if (!file.properties().empty() || !file.tables().empty()) {
            std::cout << "standalone properties:\n";
            list_properties(file.properties(), "  ");
            list_tables(file.tables(), "  ");
        }
        for (std::size_t i = 0; i < file.images().size(); ++i) {
            const openxisf::image_info& info = file.image(i);
            std::cout << "image " << i << (info.id.empty() ? "" : " '" + info.id + "'") << ":\n";
            list_image(info);
        }
        // A problem confined to one object leaves the rest of the unit readable (spec §7).
        for (const openxisf::diagnostic& entry : file.diagnostics()) {
            std::cout << severity_name(entry.severity) << ": " << entry.message;
            if (!entry.context.element.empty()) {
                std::cout << " (" << entry.context.element << ")";
            }
            std::cout << '\n';
        }
        return 0;
    } catch (const std::exception& failure) {
        (void)std::fputs(failure.what(), stderr);
        (void)std::fputs("\n", stderr);
        return 1;
    }
}
