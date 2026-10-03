// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#pragma once

#include <openxisf/error.h>
#include <openxisf/limits.h>

#include "container/data_block.h"
#include "core/diagnostic_log.h"
#include "io/thread_safe_source.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openxisf::detail {

/// The data that a unit holds beyond its header, counted against limits::max_ancillary_data: the data blocks loaded
/// when it is opened (the values of properties and table cells, ICC profiles and thumbnails), and the copies that
/// Reference elements make of what they name.
class ancillary_budget
{
public:
    /// A budget of limit bytes, where 0 means no limit.
    explicit ancillary_budget(std::uint64_t limit) noexcept : limit_(limit) {}

    /// Counts a data block of cost bytes that is about to be loaded. False when it would take the data beyond the
    /// limit, which is an error in log about context, and nothing is counted.
    [[nodiscard]] bool load(std::uint64_t cost, error_context context, diagnostic_log& log);

    /// Counts a copy of cost bytes of what, such as "the property 'Test:Name'". False when it would take the data
    /// beyond the limit, which is an error in log about context, and nothing is counted.
    [[nodiscard]] bool copy(std::uint64_t cost, std::string_view what, error_context context, diagnostic_log& log);

private:
    [[nodiscard]] bool fits(std::uint64_t cost) const noexcept;

    std::uint64_t limit_;
    std::uint64_t used_ = 0;
};

/// The data of an available block, decompressed, loaded within the budget, which counts the larger of its stored and
/// its data size. Nothing when it cannot be read, which is an error in log about the element at path. What the source
/// throws passes through.
[[nodiscard]] std::optional<std::vector<std::byte>> load_block(const thread_safe_source& source,
                                                               const block_descriptor& descriptor, const limits& limits,
                                                               ancillary_budget& budget, const std::string& path,
                                                               diagnostic_log& log);

} // namespace openxisf::detail
