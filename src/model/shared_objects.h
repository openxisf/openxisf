// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/error.h>

#include "core/diagnostic_log.h"
#include "model/ancillary_budget.h"
#include "model/held_size.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace openxisf::detail {

/// The objects read from elements, such as tables or ICC profiles, by the index of their element, given to the objects
/// they are associated with: an element can be associated with several images through Reference elements (spec
/// §11.13). The uses of each are counted first; the last use takes the object, and each one before takes a copy, which
/// counts against the budget by its held_size(), so that References cannot multiply the memory that a unit takes.
template <typename T> class shared_objects
{
public:
    explicit shared_objects(std::unordered_map<std::size_t, T> objects) : objects_(std::move(objects)) {}

    /// True when the element at index was read.
    [[nodiscard]] bool contains(std::size_t index) const
    {
        return objects_.contains(index);
    }

    /// The object of the element at index, before a use takes it, or null when the element was not read.
    [[nodiscard]] const T* find(std::size_t index) const
    {
        const auto found = objects_.find(index);
        return found == objects_.end() ? nullptr : &found->second;
    }

    /// Counts a use of the object of the element at index.
    void add_use(std::size_t index)
    {
        if (objects_.contains(index)) {
            ++uses_[index];
        }
    }

    /// The object of the element at index for one of its uses: moved on the last one, copied before. Nothing when the
    /// element was not read, or when the copy is beyond the budget, which is an error in log about context.
    std::optional<T> take(std::size_t index, std::string_view what, const error_context& context,
                          ancillary_budget& budget, diagnostic_log& log)
    {
        const auto found = objects_.find(index);
        if (found == objects_.end()) {
            return std::nullopt;
        }
        if (--uses_[index] == 0) {
            std::optional<T> last(std::move(found->second));
            objects_.erase(found);
            return last;
        }
        // Computed once for all the copies, which can be many more than the budget holds.
        const auto [size, unknown] = sizes_.try_emplace(index);
        if (unknown) {
            size->second = held_size(found->second);
        }
        if (!budget.copy(size->second, what, context, log)) {
            return std::nullopt;
        }
        return found->second;
    }

    /// Gives up a use of the object of the element at index, without taking it.
    void release(std::size_t index)
    {
        if (objects_.contains(index) && --uses_[index] == 0) {
            objects_.erase(index);
        }
    }

private:
    std::unordered_map<std::size_t, T> objects_;
    std::unordered_map<std::size_t, std::size_t> uses_{};
    std::unordered_map<std::size_t, std::uint64_t> sizes_{};
};

} // namespace openxisf::detail
