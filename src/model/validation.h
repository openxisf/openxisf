// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/writer.h>

#include "model/unit_contents.h"

namespace openxisf::detail {

/// Checks a unit before it is written, so that a unit is written whole or not at all: the rules of the specification
/// for every object it holds, and the mandatory metadata that the options give. Throws validation_error for the first
/// violation, with the path of the element that the object would be written as, and the attribute when one is at
/// fault. The path names images and the elements that describe them by their position, and properties and tables by
/// their identifier, as in /xisf/Image[2]/Property[@id='Instrument:Filter'], or by their position when the identifier
/// itself is at fault. The metadata properties that the writer writes itself (generated_metadata) are not checked,
/// since they are not written.
void validate_unit(const unit_contents& unit, const write_options& options);

} // namespace openxisf::detail
