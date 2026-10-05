// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/property.h>

#include <array>
#include <cstdint>
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

/// True when id is in the namespace of astrometric solutions, AstrometricSolution: (spec §11.5.3.7).
[[nodiscard]] bool is_astrometric_solution_id(std::string_view id) noexcept;

/// The major and minor revision of the value of AstrometricSolution:Version, "major.minor" with both plain decimal
/// unsigned integers (spec §11.5.3.7.2), or nothing when it is not of that form.
[[nodiscard]] std::optional<std::array<std::uint32_t, 2>> parse_astrometric_version(std::string_view text) noexcept;

/// True when version, the value of AstrometricSolution:Version, is a String of a revision of spec §11.5.3.7 other than
/// 1.x, or of no revision. The types of the catalogue do not apply to the other properties of the namespace of such a
/// solution, which a decoder does not read and an encoder keeps as they are (spec §11.5.3.7.6).
[[nodiscard]] bool is_foreign_astrometric_version(const property_value& version) noexcept;

/// True when the AstrometricSolution:Version of properties is a foreign version (is_foreign_astrometric_version()).
[[nodiscard]] bool has_foreign_astrometric_solution(const property_list& properties) noexcept;

} // namespace openxisf::detail
