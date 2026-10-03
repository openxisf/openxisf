// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

// The reserved property identifiers and their types (spec §11.4, §11.5.3).

#include "model/property_catalog.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

using openxisf::property_type;
using openxisf::detail::is_metadata_id;
using openxisf::detail::reserved_property_type;

TEST(property_catalog, metadata_properties)
{
    // Spec §11.4.1 and §11.4.2: every property, with its type.
    EXPECT_EQ(reserved_property_type("XISF:CreationTime"), property_type::time_point);
    EXPECT_EQ(reserved_property_type("XISF:CreatorApplication"), property_type::string);
    EXPECT_EQ(reserved_property_type("XISF:OriginalCreationTime"), property_type::time_point);
    EXPECT_EQ(reserved_property_type("XISF:BlockAlignmentSize"), property_type::uint16);
    EXPECT_EQ(reserved_property_type("XISF:MaxInlineBlockSize"), property_type::uint16);
    EXPECT_EQ(reserved_property_type("XISF:CompressionLevel"), property_type::int32);
    for (const std::string_view id :
         {"XISF:Abstract", "XISF:AccessRights", "XISF:Authors", "XISF:BibliographicReferences", "XISF:BriefDescription",
          "XISF:ChecksumAlgorithms", "XISF:CompressionCodecs", "XISF:Contributors", "XISF:Copyright",
          "XISF:CreatorModule", "XISF:CreatorOS", "XISF:Description", "XISF:Keywords", "XISF:Languages", "XISF:License",
          "XISF:OutputHints", "XISF:RelatedResources", "XISF:Title"}) {
        EXPECT_EQ(reserved_property_type(id), property_type::string) << id;
    }
}

TEST(property_catalog, astronomical_properties)
{
    // A sample of each namespace of spec §11.5.3.
    EXPECT_EQ(reserved_property_type("Observer:Name"), property_type::string);
    EXPECT_EQ(reserved_property_type("Organization:Website"), property_type::string);
    EXPECT_EQ(reserved_property_type("Observation:Center:RA"), property_type::float64);
    EXPECT_EQ(reserved_property_type("Observation:Meteorology:WindGust"), property_type::float32);
    EXPECT_EQ(reserved_property_type("Observation:Time:Start"), property_type::time_point);
    EXPECT_EQ(reserved_property_type("Instrument:ExposureTime"), property_type::float32);
    EXPECT_EQ(reserved_property_type("Instrument:Camera:ISOSpeed"), property_type::int32);
    EXPECT_EQ(reserved_property_type("Instrument:Camera:XBinning"), property_type::int32);
    EXPECT_EQ(reserved_property_type("Image:FrameNumber"), property_type::uint32);
    EXPECT_EQ(reserved_property_type("Image:SubgroupId"), property_type::string);
    EXPECT_EQ(reserved_property_type("Processing:History"), property_type::string);
}

TEST(property_catalog, astrometric_solution_properties)
{
    // Spec §11.5.3.7.
    EXPECT_EQ(reserved_property_type("AstrometricSolution:Version"), property_type::string);
    EXPECT_EQ(reserved_property_type("AstrometricSolution:ReferenceCelestialCoordinates"), property_type::f64_vector);
    EXPECT_EQ(reserved_property_type("AstrometricSolution:LinearTransformationMatrix"), property_type::f64_matrix);
    EXPECT_EQ(reserved_property_type("AstrometricSolution:ProjectiveTransformation:ProjectionToImage"),
              property_type::f64_matrix);
    EXPECT_EQ(reserved_property_type("AstrometricSolution:ControlPoints:Rejected"), property_type::i32_vector);
    EXPECT_EQ(reserved_property_type("AstrometricSolution:CreationTime"), property_type::time_point);
    // The properties of a distortion model, in both directions.
    for (const std::string_view direction : {"ImageToProjection", "ProjectionToImage"}) {
        const std::string prefix = "AstrometricSolution:DistortionModel:" + std::string(direction) + ":";
        EXPECT_EQ(reserved_property_type(prefix + "BasisFunction"), property_type::string);
        EXPECT_EQ(reserved_property_type(prefix + "Order"), property_type::int32);
        EXPECT_EQ(reserved_property_type(prefix + "Polynomial"), property_type::boolean);
        EXPECT_EQ(reserved_property_type(prefix + "Global:X:Nodes"), property_type::f64_matrix);
        EXPECT_EQ(reserved_property_type(prefix + "Global:Y:ShapeParameter"), property_type::float64);
        EXPECT_EQ(reserved_property_type(prefix + "Local:X:NodeOffsets"), property_type::i32_vector);
        EXPECT_EQ(reserved_property_type(prefix + "Local:Y:Normalization"), property_type::f64_matrix);
        EXPECT_EQ(reserved_property_type(prefix + "Local:X:ShapeParameter"), property_type::f64_vector);
        EXPECT_EQ(reserved_property_type(prefix + "Fallback:Threshold"), property_type::float64);
        EXPECT_EQ(reserved_property_type(prefix + "Fallback:Y:Normalization"), property_type::f64_vector);
        EXPECT_FALSE(reserved_property_type(prefix + "Unknown").has_value());
    }
}

TEST(property_catalog, other_identifiers_are_not_reserved)
{
    for (const std::string_view id :
         {"", "XISF", "XISF:", "XISF:Unknown", "xisf:CreationTime", "XISF:CreationTime:", "XISF:CreationTimes",
          "Observation:Center", "PixInsight:ProcessingHistory", "AstrometricSolution:DistortionModel:Order",
          "AstrometricSolution:DistortionModel:Other:Order", "AstrometricSolution:Future",
          "Instrument:ExposureTime "}) {
        EXPECT_FALSE(reserved_property_type(id).has_value()) << id;
    }
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
