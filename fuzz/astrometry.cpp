// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Astrometric solutions (spec §11.5.3.7) read from property sets built from the input. The input is a sequence of
// records, each a property: a byte that picks its identifier from the namespace (the properties of every layer, those
// of both directions of a distortion model, and others), a byte that picks its type, and its value: a String of up to
// 255 bytes, an Int32, a Float64, a Boolean, or a vector or a matrix of Float64, Int32 or Float32 elements, all little
// endian. A record that the input does not complete ends it.
//
// Reading a solution never fails, and the solution is consistent: the highest layer is the last of the available ones
// from the first, each unavailable layer that is not absent says why, and a solution of another major revision is not
// read. Every available layer evaluates points of the image and of the sky to nothing or to finite coordinates in their
// ranges, and an unavailable one refuses to. Removing the solution leaves none. Any other exception escapes and fails
// the run.

#include <openxisf/astrometry.h>
#include <openxisf/error.h>
#include <openxisf/property.h>

#include "core/endian.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace detail = openxisf::detail;
using openxisf::astrometric_layer;
using openxisf::astrometric_solution;
using openxisf::astrometric_status;
using openxisf::celestial_point;
using openxisf::image_point;
using openxisf::property_list;
using openxisf::property_value;

// Stops the run, so that the fuzzer reports the input that broke a solution.
void require(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

// The identifiers that records pick from.
std::vector<std::string> identifiers()
{
    const std::string prefix = "AstrometricSolution:";
    std::vector<std::string> ids;
    for (const char* id : {"Version", "ProjectionSystem", "ReferenceCelestialCoordinates", "ReferenceImageCoordinates",
                           "LinearTransformationMatrix", "ReferenceNativeCoordinates", "CelestialPoleNativeCoordinates",
                           "CelestialReferenceSystem", "ProjectiveTransformation:ImageToProjection",
                           "ProjectiveTransformation:ProjectionToImage", "ControlPoints:Celestial",
                           "ControlPoints:Image", "Catalog", "Unknown"}) {
        ids.push_back(prefix + id);
    }
    for (const char* direction : {"ImageToProjection:", "ProjectionToImage:"}) {
        for (const char* id : {"BasisFunction",
                               "Order",
                               "Polynomial",
                               "Terms",
                               "Global:X:Normalization",
                               "Global:X:Nodes",
                               "Global:X:Coefficients",
                               "Global:X:ShapeParameter",
                               "Global:Y:Coefficients",
                               "Global:Y:Normalization",
                               "Global:Y:Nodes",
                               "Global:Y:ShapeParameter",
                               "Local:Center",
                               "Local:Radius",
                               "Local:X:Normalization",
                               "Local:X:NodeOffsets",
                               "Local:X:Nodes",
                               "Local:X:Coefficients",
                               "Local:X:ShapeParameter",
                               "Local:Y:Coefficients",
                               "Local:Y:Normalization",
                               "Local:Y:NodeOffsets",
                               "Local:Y:Nodes",
                               "Local:Y:ShapeParameter",
                               "Fallback:Threshold",
                               "Fallback:X:Normalization",
                               "Fallback:X:Nodes",
                               "Fallback:X:Coefficients",
                               "Fallback:X:ShapeParameter",
                               "Fallback:Y:Coefficients",
                               "Fallback:Y:Normalization",
                               "Fallback:Y:Nodes",
                               "Fallback:Y:ShapeParameter"}) {
            ids.push_back(prefix + "DistortionModel:" + direction + id);
        }
    }
    ids.emplace_back("Observation:Center:RA");
    return ids;
}

class input_reader
{
public:
    explicit input_reader(std::span<const std::uint8_t> data) : data_(data) {}

    [[nodiscard]] std::optional<std::span<const std::uint8_t>> take(std::size_t count)
    {
        if (count > data_.size()) {
            return std::nullopt;
        }
        const std::span<const std::uint8_t> taken = data_.first(count);
        data_ = data_.subspan(count);
        return taken;
    }

    // An unsigned integer of Size bytes, little endian.
    template <std::unsigned_integral T> [[nodiscard]] std::optional<T> unsigned_number()
    {
        const std::optional<std::span<const std::uint8_t>> bytes = take(sizeof(T));
        if (!bytes) {
            return std::nullopt;
        }
        const std::span<const std::byte> raw = std::as_bytes(*bytes);
        return detail::load_little_endian<T>(raw.first<sizeof(T)>());
    }

    [[nodiscard]] std::optional<double> float64()
    {
        const std::optional<std::uint64_t> bits = unsigned_number<std::uint64_t>();
        return bits ? std::optional<double>(std::bit_cast<double>(*bits)) : std::nullopt;
    }

    [[nodiscard]] std::optional<float> float32()
    {
        const std::optional<std::uint32_t> bits = unsigned_number<std::uint32_t>();
        return bits ? std::optional<float>(std::bit_cast<float>(*bits)) : std::nullopt;
    }

    [[nodiscard]] std::optional<std::int32_t> int32()
    {
        const std::optional<std::uint32_t> bits = unsigned_number<std::uint32_t>();
        return bits ? std::optional<std::int32_t>(std::bit_cast<std::int32_t>(*bits)) : std::nullopt;
    }

    // count elements read by next, or nothing when the input ends first.
    template <typename T, typename Next>
    [[nodiscard]] std::optional<std::vector<T>> elements(std::size_t count, Next next)
    {
        std::vector<T> values;
        for (std::size_t i = 0; i < count; ++i) {
            const std::optional<T> value = next();
            if (!value) {
                return std::nullopt;
            }
            values.push_back(*value);
        }
        return values;
    }

private:
    std::span<const std::uint8_t> data_;
};

// The value of a record of the given kind, or nothing when the input ends first.
std::optional<property_value> read_value(input_reader& input, std::uint8_t kind)
{
    switch (kind % 8) {
    case 0: {
        const std::optional<std::uint8_t> length = input.unsigned_number<std::uint8_t>();
        const std::optional<std::span<const std::uint8_t>> text = length ? input.take(*length) : std::nullopt;
        if (!text) {
            return std::nullopt;
        }
        return property_value(std::string(text->begin(), text->end()));
    }
    case 1:
        if (const std::optional<std::int32_t> value = input.int32()) {
            return property_value(*value);
        }
        return std::nullopt;
    case 2:
        if (const std::optional<double> value = input.float64()) {
            return property_value(*value);
        }
        return std::nullopt;
    case 3:
        if (const std::optional<std::uint8_t> value = input.unsigned_number<std::uint8_t>()) {
            return property_value((*value & 1U) != 0);
        }
        return std::nullopt;
    case 4: {
        const std::optional<std::uint16_t> count = input.unsigned_number<std::uint16_t>();
        if (std::optional<std::vector<double>> values =
                count ? input.elements<double>(*count, [&] { return input.float64(); }) : std::nullopt) {
            return property_value(std::move(*values));
        }
        return std::nullopt;
    }
    case 5: {
        const std::optional<std::uint16_t> rows = input.unsigned_number<std::uint16_t>();
        const std::optional<std::uint8_t> columns = rows ? input.unsigned_number<std::uint8_t>() : std::nullopt;
        if (!rows || !columns) {
            return std::nullopt;
        }
        if (std::optional<std::vector<double>> values =
                input.elements<double>(std::size_t{*rows} * std::size_t{*columns}, [&] { return input.float64(); })) {
            return property_value::matrix(*rows, *columns, std::move(*values));
        }
        return std::nullopt;
    }
    case 6: {
        const std::optional<std::uint16_t> count = input.unsigned_number<std::uint16_t>();
        if (std::optional<std::vector<std::int32_t>> values =
                count ? input.elements<std::int32_t>(*count, [&] { return input.int32(); }) : std::nullopt) {
            return property_value(std::move(*values));
        }
        return std::nullopt;
    }
    default: {
        const std::optional<std::uint8_t> count = input.unsigned_number<std::uint8_t>();
        if (std::optional<std::vector<float>> values =
                count ? input.elements<float>(*count, [&] { return input.float32(); }) : std::nullopt) {
            return property_value(std::move(*values));
        }
        return std::nullopt;
    }
    }
}

property_list read_properties(std::span<const std::uint8_t> data)
{
    static const std::vector<std::string> ids = identifiers();
    input_reader input(data);
    property_list properties;
    while (true) {
        const std::optional<std::uint8_t> id = input.unsigned_number<std::uint8_t>();
        const std::optional<std::uint8_t> kind = id ? input.unsigned_number<std::uint8_t>() : std::nullopt;
        std::optional<property_value> value = kind ? read_value(input, *kind) : std::nullopt;
        if (!value) {
            return properties;
        }
        properties.set(ids[*id % ids.size()], std::move(*value));
    }
}

void check_sky(const std::optional<celestial_point>& sky)
{
    if (sky) {
        require(std::isfinite(sky->ra) && sky->ra >= 0.0 && sky->ra < 360.0);
        require(std::isfinite(sky->dec) && std::abs(sky->dec) <= 90.0);
    }
}

void check_image(const std::optional<image_point>& image)
{
    if (image) {
        require(std::isfinite(image->x) && std::isfinite(image->y));
    }
}

void check_layers(const astrometric_solution& solved)
{
    constexpr std::array<astrometric_layer, 3> layers{astrometric_layer::linear, astrometric_layer::projective,
                                                      astrometric_layer::distortion};
    const std::optional<astrometric_layer> highest = solved.layer();
    bool below_available = true;
    for (const astrometric_layer layer : layers) {
        const astrometric_status status = solved.status(layer);
        const bool available = status == astrometric_status::available;
        // The available layers are the first ones, up to the highest.
        require(available == (below_available && highest && layer <= *highest));
        require(solved.problem(layer).empty() == (available || status == astrometric_status::absent));
        if (status == astrometric_status::unsupported_version) {
            require(solved.status(astrometric_layer::linear) == astrometric_status::unsupported_version);
        }
        below_available = below_available && available;
    }
    require(solved.projection().has_value() == highest.has_value());
}

void evaluate(const astrometric_solution& solved)
{
    const std::optional<openxisf::astrometric_projection>& projection = solved.projection();
    const image_point reference = projection ? projection->reference_image : image_point{};
    const celestial_point sky = projection ? projection->reference_celestial : celestial_point{};
    const std::array<image_point, 4> images{image_point{}, reference,
                                            image_point{.x = reference.x + 100.5, .y = reference.y - 75.25},
                                            image_point{.x = reference.x - 1000.0, .y = reference.y + 1e6}};
    const std::array<celestial_point, 4> skies{
        sky, celestial_point{.ra = sky.ra + 1.0, .dec = std::clamp(sky.dec - 1.0, -90.0, 90.0)},
        celestial_point{.ra = 0.0, .dec = 90.0}, celestial_point{.ra = 180.0, .dec = -45.0}};
    for (const astrometric_layer layer :
         {astrometric_layer::linear, astrometric_layer::projective, astrometric_layer::distortion}) {
        if (solved.status(layer) != astrometric_status::available) {
            try {
                (void)solved.image_to_celestial(reference, layer);
                require(false);
            } catch (const openxisf::usage_error& failure) {
                require(failure.code() == openxisf::errc::invalid_argument);
            }
            continue;
        }
        for (const image_point point : images) {
            check_sky(solved.image_to_celestial(point, layer));
        }
        for (const celestial_point point : skies) {
            check_image(solved.celestial_to_image(point, layer));
        }
    }
}

} // namespace

// The name is fixed by libFuzzer.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    property_list properties = read_properties(std::span(data, size));
    const astrometric_solution solved(properties);
    check_layers(solved);
    evaluate(solved);
    if (const openxisf::property* version = properties.find("AstrometricSolution:Version");
        version != nullptr && version->value.type() == openxisf::property_type::string) {
        if (solved.status(astrometric_layer::linear) == astrometric_status::unsupported_version) {
            require(solved.status(astrometric_layer::distortion) == astrometric_status::unsupported_version);
        }
    }
    openxisf::remove_astrometric_solution(properties);
    require(astrometric_solution(properties).status(astrometric_layer::linear) == astrometric_status::absent);
    return 0;
}
