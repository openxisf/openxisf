// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <openxisf/openxisf.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// The I/O classes across the boundary of a shared library: a source of the library read through its interface, and an
// exception of the library caught by its type.
bool reads_through_the_library()
{
    const std::array<std::byte, 3> data{std::byte{1}, std::byte{2}, std::byte{3}};
    const openxisf::memory_source source{std::span<const std::byte>(data)};
    const openxisf::input_source& any = source;
    std::array<std::byte, 2> destination{};
    any.read(1, destination);
    if (destination != std::array<std::byte, 2>{std::byte{2}, std::byte{3}}) {
        return false;
    }
    try {
        any.read(2, destination);
    } catch (const openxisf::io_error& failure) {
        return failure.code() == openxisf::errc::end_of_data;
    }
    return false;
}

std::unique_ptr<openxisf::input_source> unit_of(std::string_view header)
{
    std::vector<std::byte> unit;
    for (const char c : std::string_view("XISF0100")) {
        unit.push_back(static_cast<std::byte>(c));
    }
    for (int shift = 0; shift < 64; shift += 8) {
        // The header length, then the reserved field. Both are little-endian.
        unit.push_back(static_cast<std::byte>(shift < 32 ? (header.size() >> shift) & 0xFFU : 0U));
    }
    for (const char c : header) {
        unit.push_back(static_cast<std::byte>(c));
    }
    return std::make_unique<openxisf::memory_source>(std::move(unit));
}

// The reader across the boundary: a unit opened with its diagnostics, and a unit refused with an exception caught by
// its type.
bool opens_units_through_the_library()
{
    const openxisf::reader file(unit_of(R"(<?xml version="1.0" encoding="UTF-8"?><xisf version="1.0" )"
                                        R"(xmlns="http://www.pixinsight.com/xisf"><Metadata/><Unknown/></xisf>)"));
    const auto unknown = [](const openxisf::diagnostic& entry) {
        return entry.code == openxisf::errc::unknown_element && entry.context.element == "/xisf/Unknown[1]";
    };
    if (file.storage() != openxisf::unit_storage::monolithic || !std::ranges::any_of(file.diagnostics(), unknown)) {
        return false;
    }
    try {
        const openxisf::reader refused(unit_of(R"(<?xml version="1.0" encoding="UTF-8"?><xisf version="1.0">)"));
    } catch (const openxisf::invalid_data_error& failure) {
        return failure.code() == openxisf::errc::invalid_header_length;
    }
    return false;
}

// The properties across the boundary: values held in the types of the library, and its checked accessors.
bool reads_properties_through_the_library()
{
    const openxisf::reader file(unit_of(
        R"(<?xml version="1.0" encoding="UTF-8"?><xisf version="1.0" xmlns="http://www.pixinsight.com/xisf"><Metadata>)"
        R"(<Property id="XISF:CreationTime" type="TimePoint" value="2026-10-03T12:00:00Z"/>)"
        R"(<Property id="XISF:CreatorApplication" type="String">consumer 1.0</Property></Metadata>)"
        R"(<Property id="Gains" type="F64Vector" length="2" location="inline:hex">)"
        R"(000000000000f03f0000000000000040</Property></xisf>)"));
    if (!file.diagnostics().empty() || file.metadata().size() != 2 ||
        file.metadata().at("XISF:CreatorApplication").value.get<std::string>() != "consumer 1.0" ||
        file.metadata().at("XISF:CreationTime").value.get<openxisf::date_time>().hour != 12) {
        return false;
    }
    const openxisf::property_value& gains = file.properties().at("Gains").value;
    if (gains.type() != openxisf::property_type::f64_vector || gains.length() != 2 ||
        gains.elements<double>()[1] != 2.0 || openxisf::property_type_name(gains.type()) != "F64Vector") {
        return false;
    }
    try {
        (void)gains.get<float>();
    } catch (const openxisf::usage_error& failure) {
        return failure.code() == openxisf::errc::invalid_argument;
    }
    return false;
}

// The images across the boundary: the description of an image, its pixels read in another storage model, a typed read,
// whose template calls into the library, and a typed read of the wrong type refused with a usage_error.
bool reads_pixels_through_the_library()
{
    const openxisf::reader file(unit_of(
        R"(<?xml version="1.0" encoding="UTF-8"?><xisf version="1.0" xmlns="http://www.pixinsight.com/xisf"><Metadata>)"
        R"(<Property id="XISF:CreationTime" type="TimePoint" value="2026-10-03T12:00:00Z"/>)"
        R"(<Property id="XISF:CreatorApplication" type="String">consumer 1.0</Property></Metadata>)"
        R"(<Image id="rgb" geometry="2:1:3" sampleFormat="UInt16" colorSpace="RGB" location="embedded">)"
        R"(<Data encoding="hex">010002000300040005000600</Data></Image></xisf>)"));
    if (!file.diagnostics().empty() || file.images().size() != 1 || file.image(0).id != "rgb" ||
        file.image(0).color_space != openxisf::color_space::rgb || file.image(0).data_size() != 12) {
        return false;
    }
    const std::vector<std::uint16_t> normal =
        file.read_pixels<std::uint16_t>(0, {.storage = openxisf::pixel_storage::normal});
    if (normal != std::vector<std::uint16_t>{1, 3, 5, 2, 4, 6} || file.read_pixels(0).size() != 12) {
        return false;
    }
    try {
        (void)file.read_pixels<float>(0);
    } catch (const openxisf::usage_error& failure) {
        return failure.code() == openxisf::errc::invalid_argument;
    }
    return false;
}

// The elements that describe images across the boundary: keywords, a thumbnail and a table in the types of the
// library, a header-only open, and the loader, which changes the reader through an exported member.
bool reads_descriptions_through_the_library()
{
    const std::string_view header =
        R"(<?xml version="1.0" encoding="UTF-8"?><xisf version="1.0" xmlns="http://www.pixinsight.com/xisf"><Metadata>)"
        R"(<Property id="XISF:CreationTime" type="TimePoint" value="2026-10-03T12:00:00Z"/>)"
        R"(<Property id="XISF:CreatorApplication" type="String">consumer 1.0</Property></Metadata>)"
        R"(<Image geometry="1:1:1" sampleFormat="UInt8" location="embedded"><Data encoding="hex">07</Data>)"
        R"(<FITSKeyword name="EXPTIME" value="300" comment="Exposure time in seconds"/>)"
        R"(<Resolution horizontal="120" vertical="120" unit="cm"/>)"
        R"(<Thumbnail geometry="1:1:1" sampleFormat="UInt8" colorSpace="Gray" location="embedded">)"
        R"(<Data encoding="hex">2a</Data></Thumbnail>)"
        R"(<Table id="Stars"><Structure><Field id="name" type="String"/></Structure>)"
        R"(<Row><Cell>Vega</Cell></Row></Table></Image></xisf>)";
    openxisf::reader file(unit_of(header), {.header_only = true});
    if (!file.diagnostics().empty() || file.ancillary_data_loaded() || file.images().size() != 1) {
        return false;
    }
    // Loading invalidates what the reader returned, so the description is not kept beyond this check.
    if (const openxisf::image_info& described = file.image(0);
        !described.thumbnail || !described.thumbnail->pixels.empty()) {
        return false;
    }
    file.load_ancillary_data();
    const openxisf::image_info& info = file.image(0);
    return file.ancillary_data_loaded() && info.fits_keywords.size() == 1 && info.fits_keywords[0].value == "300" &&
           info.resolution && info.resolution->unit == openxisf::resolution_unit::centimeter && info.thumbnail &&
           info.thumbnail->pixels == std::vector<std::byte>{std::byte{0x2a}} && info.tables.size() == 1 &&
           info.tables[0].rows.at(0).at(0).get<std::string>() == "Vega";
}

// The writer across the boundary: a unit written into a sink of the library, a typed image, which calls an exported
// member from a template, and a validation error caught by its type.
bool writes_through_the_library()
{
    openxisf::writer output({.creator_application = "consumer 1.0",
                             .codec = openxisf::codec::zstd,
                             .byte_shuffle = true,
                             .checksum = openxisf::checksum_algorithm::sha1});
    openxisf::image_info info;
    info.geometry = {.dimensions = {3, 2}, .channels = 1};
    info.properties.set("Instrument:ExposureTime", 300.0F);
    const std::vector<std::uint16_t> samples{1, 2, 3, 4, 5, 6};
    (void)output.add_image(info, std::span<const std::uint16_t>(samples));
    openxisf::memory_sink sink;
    output.save(sink);
    const openxisf::reader file(std::make_unique<openxisf::memory_source>(sink.release()), {.strict = true});
    if (file.read_pixels<std::uint16_t>(0) != samples || file.image(0).properties != info.properties) {
        return false;
    }
    try {
        openxisf::writer invalid({.creator_application = "no version"});
        invalid.save(sink);
    } catch (const openxisf::validation_error& failure) {
        return failure.code() == openxisf::errc::invalid_metadata;
    }
    return false;
}

// A distributed unit across the boundary: written into two sinks, read with a resolver of the application, and the file
// resolver of the library, which refuses a path outside its directory.
bool reads_distributed_units_through_the_library()
{
    openxisf::writer output({.creator_application = "consumer 1.0"});
    openxisf::image_info info;
    info.geometry = {.dimensions = {2, 2}, .channels = 1};
    const std::vector<std::uint16_t> samples{1, 2, 3, 4};
    (void)output.add_image(info, std::span<const std::uint16_t>(samples));
    openxisf::memory_sink header;
    auto blocks = std::make_shared<openxisf::memory_sink>();
    output.save_distributed(header, *blocks, "unit.xisb");
    const openxisf::reader file(std::make_unique<openxisf::memory_source>(header.release()),
                                {.strict = true, .resolver = [blocks](const openxisf::external_reference& reference) {
                                     return reference.location == "unit.xisb"
                                                ? std::make_unique<openxisf::memory_source>(blocks->data())
                                                : nullptr;
                                 }});
    if (file.storage() != openxisf::unit_storage::distributed || file.read_pixels<std::uint16_t>(0) != samples) {
        return false;
    }
    try {
        (void)openxisf::file_resolver(".")(
            {.form = openxisf::location_form::relative_path, .location = "../unit.xisb"});
    } catch (const openxisf::unsupported_error& failure) {
        return failure.code() == openxisf::errc::location_not_allowed;
    } catch (const openxisf::io_error& failure) {
        // The working directory has no parent with that file.
        return failure.code() == openxisf::errc::open_failed;
    }
    return false;
}

} // namespace

// Built against an installed package. It fails when the header, the library and the package version file
// do not agree.
int main()
{
    try {
        const std::string_view linked = openxisf::version();
        std::cout << "openxisf " << linked << '\n';

        if (linked != OPENXISF_VERSION_STRING) {
            std::cerr << "the library reports " << linked << ", the header says " << OPENXISF_VERSION_STRING << '\n';
            return 1;
        }
        if (linked != EXPECTED_VERSION) {
            std::cerr << "the library reports " << linked << ", find_package found " << EXPECTED_VERSION << '\n';
            return 1;
        }
        if (!reads_through_the_library()) {
            std::cerr << "a memory source of the library does not read as expected\n";
            return 1;
        }
        if (!opens_units_through_the_library()) {
            std::cerr << "the reader of the library does not open units as expected\n";
            return 1;
        }
        if (!reads_pixels_through_the_library()) {
            std::cerr << "the images of the library do not read as expected\n";
            return 1;
        }
        if (!reads_properties_through_the_library()) {
            std::cerr << "the properties of the library do not read as expected\n";
            return 1;
        }
        if (!reads_descriptions_through_the_library()) {
            std::cerr << "the elements that describe images do not read as expected\n";
            return 1;
        }
        if (!writes_through_the_library()) {
            std::cerr << "the writer of the library does not write units as expected\n";
            return 1;
        }
        if (!reads_distributed_units_through_the_library()) {
            std::cerr << "the distributed units of the library do not read as expected\n";
            return 1;
        }
        return 0;
    } catch (...) {
        // A failing stream must not turn into a crash that hides which check was running.
        return 2;
    }
}
