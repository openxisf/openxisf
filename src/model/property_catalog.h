// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#pragma once

#include <openxisf/property.h>

#include <optional>
#include <string_view>

// The reserved property identifiers of the specification and their types: the metadata properties of the XISF
// namespace (spec §11.4), and the astronomical properties of the Observer, Organization, Observation, Instrument,
// Image, Processing and AstrometricSolution namespaces (spec §11.5.3).

namespace openxisf::detail {

/// The type that the specification gives the property id, or nothing when id is not a reserved identifier.
[[nodiscard]] std::optional<property_type> reserved_property_type(std::string_view id) noexcept;

/// True when id is in the namespace of the metadata properties, XISF: (spec §11.4).
[[nodiscard]] bool is_metadata_id(std::string_view id) noexcept;

} // namespace openxisf::detail
