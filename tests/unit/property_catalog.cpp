// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The reserved property identifiers and their types (spec §11.4, §11.5.3).

#include "model/property_catalog.h"

#include <openxisf/property.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using openxisf::property_type;
using openxisf::detail::is_metadata_id;
using openxisf::detail::reserved_property_type;

// Every reserved identifier of spec §11.4 and §11.5.3 with its type, transcribed from the definitions of the
// specification apart from the catalogue, in their order there.
std::map<std::string, property_type, std::less<>> specified_properties()
{
    using enum property_type;
    std::map<std::string, property_type, std::less<>> specified{
        // Spec §11.4.1
        {"XISF:CreationTime", time_point},
        {"XISF:CreatorApplication", string},
        // Spec §11.4.2
        {"XISF:Abstract", string},
        {"XISF:AccessRights", string},
        {"XISF:Authors", string},
        {"XISF:BibliographicReferences", string},
        {"XISF:BlockAlignmentSize", uint16},
        {"XISF:BriefDescription", string},
        {"XISF:ChecksumAlgorithms", string},
        {"XISF:CompressionLevel", int32},
        {"XISF:CompressionCodecs", string},
        {"XISF:Contributors", string},
        {"XISF:Copyright", string},
        {"XISF:CreatorModule", string},
        {"XISF:CreatorOS", string},
        {"XISF:Description", string},
        {"XISF:Keywords", string},
        {"XISF:Languages", string},
        {"XISF:License", string},
        {"XISF:MaxInlineBlockSize", uint16},
        {"XISF:OriginalCreationTime", time_point},
        {"XISF:OutputHints", string},
        {"XISF:RelatedResources", string},
        {"XISF:Title", string},
        // Spec §11.5.3.1
        {"Observer:EmailAddress", string},
        {"Observer:Name", string},
        {"Observer:PostalAddress", string},
        {"Observer:Website", string},
        // Spec §11.5.3.2
        {"Organization:EmailAddress", string},
        {"Organization:Name", string},
        {"Organization:PostalAddress", string},
        {"Organization:Website", string},
        // Spec §11.5.3.3
        {"Observation:BibliographicReferences", string},
        {"Observation:CelestialReferenceSystem", string},
        {"Observation:Center:Dec", float64},
        {"Observation:Center:RA", float64},
        {"Observation:Center:X", float64},
        {"Observation:Center:Y", float64},
        {"Observation:Description", string},
        {"Observation:Equinox", float64},
        {"Observation:GeodeticReferenceSystem", string},
        {"Observation:Location:Elevation", float64},
        {"Observation:Location:Latitude", float64},
        {"Observation:Location:Longitude", float64},
        {"Observation:Location:Name", string},
        {"Observation:Meteorology:AmbientTemperature", float32},
        {"Observation:Meteorology:AtmosphericPressure", float32},
        {"Observation:Meteorology:RelativeHumidity", float32},
        {"Observation:Meteorology:WindDirection", float32},
        {"Observation:Meteorology:WindGust", float32},
        {"Observation:Meteorology:WindSpeed", float32},
        {"Observation:Object:Dec", float64},
        {"Observation:Object:Name", string},
        {"Observation:Object:RA", float64},
        {"Observation:RelatedResources", string},
        {"Observation:Time:End", time_point},
        {"Observation:Time:Start", time_point},
        {"Observation:Title", string},
        // Spec §11.5.3.4
        {"Instrument:Camera:Gain", float32},
        {"Instrument:Camera:ISOSpeed", int32},
        {"Instrument:Camera:Name", string},
        {"Instrument:Camera:ReadoutNoise", float32},
        {"Instrument:Camera:Rotation", float32},
        {"Instrument:Camera:XBinning", int32},
        {"Instrument:Camera:YBinning", int32},
        {"Instrument:ExposureTime", float32},
        {"Instrument:Filter:Name", string},
        {"Instrument:Focuser:Position", float32},
        {"Instrument:Sensor:TargetTemperature", float32},
        {"Instrument:Sensor:Temperature", float32},
        {"Instrument:Sensor:XPixelSize", float32},
        {"Instrument:Sensor:YPixelSize", float32},
        {"Instrument:Telescope:Aperture", float32},
        {"Instrument:Telescope:CollectingArea", float32},
        {"Instrument:Telescope:FocalLength", float32},
        {"Instrument:Telescope:Name", string},
        // Spec §11.5.3.5
        {"Image:FrameNumber", uint32},
        {"Image:GroupId", string},
        {"Image:SubgroupId", string},
        // Spec §11.5.3.6
        {"Processing:Description", string},
        {"Processing:History", string},
        // Spec §11.5.3.7.2
        {"AstrometricSolution:Version", string},
        {"AstrometricSolution:ProjectionSystem", string},
        {"AstrometricSolution:ReferenceCelestialCoordinates", f64_vector},
        {"AstrometricSolution:ReferenceImageCoordinates", f64_vector},
        {"AstrometricSolution:LinearTransformationMatrix", f64_matrix},
        {"AstrometricSolution:ReferenceNativeCoordinates", f64_vector},
        {"AstrometricSolution:CelestialPoleNativeCoordinates", f64_vector},
        {"AstrometricSolution:CelestialReferenceSystem", string},
        // Spec §11.5.3.7.3
        {"AstrometricSolution:ProjectiveTransformation:ImageToProjection", f64_matrix},
        {"AstrometricSolution:ProjectiveTransformation:ProjectionToImage", f64_matrix},
        // Spec §11.5.3.7.5
        {"AstrometricSolution:ControlPoints:Celestial", f64_matrix},
        {"AstrometricSolution:ControlPoints:Image", f64_matrix},
        {"AstrometricSolution:Weights", f64_vector},
        {"AstrometricSolution:ControlPoints:Rejected", i32_vector},
        {"AstrometricSolution:Catalog", string},
        {"AstrometricSolution:CreationTime", time_point},
        {"AstrometricSolution:CreatorApplication", string},
        {"AstrometricSolution:CreatorModule", string},
        {"AstrometricSolution:CreatorOS", string},
    };
    // Spec §11.5.3.7.4.4: the properties of a distortion model, under the prefix of each direction.
    const std::vector<std::pair<std::string_view, property_type>> distortion_model{
        {"BasisFunction", string},
        {"Order", int32},
        {"Polynomial", boolean},
        {"Terms", string},
        {"Global:X:Normalization", f64_vector},
        {"Global:X:Nodes", f64_matrix},
        {"Global:X:Coefficients", f64_vector},
        {"Global:X:ShapeParameter", float64},
        {"Global:Y:Coefficients", f64_vector},
        {"Global:Y:Normalization", f64_vector},
        {"Global:Y:Nodes", f64_matrix},
        {"Global:Y:ShapeParameter", float64},
        {"Local:Center", f64_matrix},
        {"Local:Radius", f64_vector},
        {"Local:X:Normalization", f64_matrix},
        {"Local:X:NodeOffsets", i32_vector},
        {"Local:X:Nodes", f64_matrix},
        {"Local:X:Coefficients", f64_vector},
        {"Local:X:ShapeParameter", f64_vector},
        {"Local:Y:Coefficients", f64_vector},
        {"Local:Y:Normalization", f64_matrix},
        {"Local:Y:NodeOffsets", i32_vector},
        {"Local:Y:Nodes", f64_matrix},
        {"Local:Y:ShapeParameter", f64_vector},
        {"Fallback:Threshold", float64},
        {"Fallback:X:Normalization", f64_vector},
        {"Fallback:X:Nodes", f64_matrix},
        {"Fallback:X:Coefficients", f64_vector},
        {"Fallback:X:ShapeParameter", float64},
        {"Fallback:Y:Coefficients", f64_vector},
        {"Fallback:Y:Normalization", f64_vector},
        {"Fallback:Y:Nodes", f64_matrix},
        {"Fallback:Y:ShapeParameter", float64},
    };
    for (const std::string_view direction : {"ImageToProjection", "ProjectionToImage"}) {
        for (const auto& [name, type] : distortion_model) {
            specified.emplace("AstrometricSolution:DistortionModel:" + std::string(direction) + ":" + std::string(name),
                              type);
        }
    }
    return specified;
}

TEST(property_catalog, holds_every_reserved_identifier_with_its_type_and_no_other)
{
    const std::map<std::string, property_type, std::less<>> specified = specified_properties();
    for (const auto& [id, type] : specified) {
        EXPECT_EQ(reserved_property_type(id), type) << id;
    }
    // The other way: the catalogue lists nothing that the specification does not, and each entry once.
    const std::vector<std::pair<std::string, property_type>> catalogue = openxisf::detail::reserved_property_types();
    for (const auto& [id, type] : catalogue) {
        EXPECT_TRUE(specified.contains(id)) << id;
        EXPECT_EQ(reserved_property_type(id), type) << id;
    }
    EXPECT_EQ(catalogue.size(), specified.size());
}

TEST(property_catalog, other_identifiers_are_not_reserved)
{
    for (const std::string_view id :
         {"", "XISF", "XISF:", "XISF:Unknown", "xisf:CreationTime", "XISF:CreationTime:", "XISF:CreationTimes",
          "Observation:Center", "PixInsight:ProcessingHistory", "AstrometricSolution:DistortionModel:Order",
          "AstrometricSolution:DistortionModel:Other:Order",
          "AstrometricSolution:DistortionModel:ImageToProjection:Unknown",
          "AstrometricSolution:DistortionModel:ProjectionToImage:Global:X:NodeOffsets", "AstrometricSolution:Future",
          "Instrument:ExposureTime "}) {
        EXPECT_FALSE(reserved_property_type(id).has_value()) << id;
    }
}

TEST(property_catalog, the_versions_of_astrometric_solutions)
{
    using openxisf::detail::parse_astrometric_version;
    using revision = std::array<std::uint32_t, 2>;
    // Spec §11.5.3.7.2: major.minor, of plain decimal unsigned integers.
    EXPECT_EQ(parse_astrometric_version("1.0"), (revision{1, 0}));
    EXPECT_EQ(parse_astrometric_version("1.12"), (revision{1, 12}));
    EXPECT_EQ(parse_astrometric_version("2.0"), (revision{2, 0}));
    EXPECT_EQ(parse_astrometric_version("01.00"), (revision{1, 0}));
    EXPECT_EQ(parse_astrometric_version("4294967295.4294967295"), (revision{4294967295U, 4294967295U}));
    for (const std::string_view text : {"", "1", "1.", ".0", "1.0.0", "+1.0", "-1.0", " 1.0", "1.0 ", "1.x", "0x1.0",
                                        "1,0", "4294967296.0", "1.4294967296"}) {
        EXPECT_FALSE(parse_astrometric_version(text).has_value()) << text;
    }
}

TEST(property_catalog, a_solution_of_another_revision_has_types_of_its_own)
{
    using openxisf::property_list;
    using openxisf::detail::has_foreign_astrometric_solution;
    property_list properties;
    EXPECT_FALSE(has_foreign_astrometric_solution(properties));
    properties.set("AstrometricSolution:Version", "1.0");
    EXPECT_FALSE(has_foreign_astrometric_solution(properties));
    properties.set("AstrometricSolution:Version", "1.7");
    EXPECT_FALSE(has_foreign_astrometric_solution(properties));
    properties.set("AstrometricSolution:Version", "2.0");
    EXPECT_TRUE(has_foreign_astrometric_solution(properties));
    properties.set("AstrometricSolution:Version", "one");
    EXPECT_TRUE(has_foreign_astrometric_solution(properties));
    // A version that is not a String is one of revision 1 of another type.
    properties.set("AstrometricSolution:Version", 2.0);
    EXPECT_FALSE(has_foreign_astrometric_solution(properties));

    EXPECT_TRUE(openxisf::detail::is_astrometric_solution_id("AstrometricSolution:Version"));
    EXPECT_TRUE(openxisf::detail::is_astrometric_solution_id("AstrometricSolution:Future"));
    EXPECT_FALSE(openxisf::detail::is_astrometric_solution_id("PCL:AstrometricSolution:Grid:Fingerprint"));
    EXPECT_FALSE(openxisf::detail::is_astrometric_solution_id("AstrometricSolutions:Version"));
}

TEST(property_catalog, the_metadata_namespace)
{
    EXPECT_TRUE(is_metadata_id("XISF:CreationTime"));
    EXPECT_TRUE(is_metadata_id("XISF:Unknown"));
    EXPECT_FALSE(is_metadata_id("XISFCreationTime"));
    EXPECT_FALSE(is_metadata_id("xisf:CreationTime"));
    EXPECT_FALSE(is_metadata_id("Observer:Name"));
}

} // namespace
