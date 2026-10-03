// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "codec/compression.h"

namespace openxisf::detail {

std::string_view codec_name(compression_codec codec) noexcept
{
    switch (codec) {
    case compression_codec::zlib:
        return "zlib";
    case compression_codec::lz4:
        return "LZ4";
    case compression_codec::lz4hc:
        return "LZ4HC";
    case compression_codec::zstd:
        return "Zstandard";
    }
    return "an unknown codec";
}

} // namespace openxisf::detail
