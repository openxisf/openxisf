// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The Base64 and Base16 encodings of inline and embedded data blocks (spec §10.3, RFC 4648). White space is ignored
// on input (spec §10.3) and never written. Decoders throw invalid_data_error.

namespace openxisf::detail {

/// Base64 text of data, with padding.
[[nodiscard]] std::string encode_base64(std::span<const std::byte> data);

/// Decodes Base64 text in its canonical form: the RFC 4648 alphabet, padding up to a multiple of four characters,
/// and zero bits after the last byte.
[[nodiscard]] std::vector<std::byte> decode_base64(std::string_view text);

/// Lowercase hexadecimal text of data.
[[nodiscard]] std::string encode_hex(std::span<const std::byte> data);

/// Decodes hexadecimal text: pairs of the lowercase digits that spec §10.3 requires.
[[nodiscard]] std::vector<std::byte> decode_hex(std::string_view text);

} // namespace openxisf::detail
