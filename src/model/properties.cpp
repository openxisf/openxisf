// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "model/properties.h"

#include <openxisf/error.h>

#include "core/quote.h"
#include "model/format_specifier.h"
#include "model/held_size.h"
#include "model/property_catalog.h"
#include "model/property_text.h"
#include "model/property_types.h"

#include <array>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace openxisf::detail {

namespace {

// The metadata properties that every unit must have (spec §11.4.1).
constexpr std::array<std::string_view, 2> mandatory_metadata{"XISF:CreationTime", "XISF:CreatorApplication"};

// The properties of one object, as they are associated with it, each identifier once.
struct property_set
{
    std::vector<property> items{};
    std::unordered_set<std::string> ids{};
};

class property_reader
{
public:
    property_reader(const unit_outline& outline, const std::vector<data_block>& blocks, value_reader& values,
                    ancillary_budget& budget, diagnostic_log& log)
        : outline_(outline), blocks_(blocks), values_(values), budget_(budget), log_(log),
          slots_(outline.elements.size(), no_slot)
    {}

    unit_properties read()
    {
        read_elements();
        count_uses();
        unit_properties result;
        result.metadata = metadata();
        result.standalone = collect(no_element);
        for (std::size_t index = 0; index < outline_.elements.size(); ++index) {
            const element_kind kind = outline_.elements[index].kind;
            if (kind == element_kind::image || kind == element_kind::thumbnail) {
                result.objects.push_back(
                    {.element = index, .path = outline_.path(index), .properties = collect(index)});
            }
        }
        return result;
    }

private:
    static constexpr std::size_t no_slot = std::numeric_limits<std::size_t>::max();

    // Reads every Property element once, in document order, with its block: the blocks are in document order too.
    void read_elements()
    {
        std::size_t next_block = 0;
        for (std::size_t index = 0; index < outline_.elements.size(); ++index) {
            while (next_block < blocks_.size() && blocks_[next_block].element < index) {
                ++next_block;
            }
            if (outline_.elements[index].kind != element_kind::property) {
                continue;
            }
            const data_block* block =
                next_block < blocks_.size() && blocks_[next_block].element == index ? &blocks_[next_block] : nullptr;
            slots_[index] = properties_.size();
            properties_.push_back(read_element(index, block));
        }
    }

    // The number of objects that each property is associated with: the one that contains it, and one for each
    // Reference that names it. The last association takes the property, the others copy it.
    void count_uses()
    {
        uses_.assign(properties_.size(), 1);
        for (const outline_element& element : outline_.elements) {
            if (element.kind == element_kind::reference && follows_references(element.parent) &&
                names_property(element)) {
                ++uses_[slots_[element.target]];
            }
        }
    }

    // Metadata, Image and Thumbnail elements have properties through their Reference elements.
    [[nodiscard]] bool follows_references(std::size_t owner) const noexcept
    {
        if (owner == no_element) {
            return false;
        }
        const element_kind kind = outline_.elements[owner].kind;
        return kind == element_kind::metadata || kind == element_kind::image || kind == element_kind::thumbnail;
    }

    [[nodiscard]] bool names_property(const outline_element& reference) const noexcept
    {
        return reference.target != no_element && outline_.elements[reference.target].kind == element_kind::property;
    }

    // Calls visit with the index of every element that owner contains directly, in document order. no_element is the
    // root element.
    template <typename Visit> void for_each_child(std::size_t owner, const Visit& visit) const
    {
        const std::size_t first = owner == no_element ? 0 : owner + 1;
        const std::size_t end = owner == no_element ? outline_.elements.size() : outline_.elements[owner].end;
        for (std::size_t index = first; index < end; ++index) {
            if (outline_.elements[index].parent == owner) {
                visit(index);
            }
        }
    }

    // The property that the element at index contains or names, if any, and the index of the Property element.
    [[nodiscard]] std::optional<std::size_t> property_of(std::size_t index) const noexcept
    {
        const outline_element& element = outline_.elements[index];
        if (element.kind == element_kind::property) {
            return index;
        }
        if (element.kind == element_kind::reference && follows_references(element.parent) && names_property(element)) {
            return element.target;
        }
        return std::nullopt;
    }

    // Associates the property of the Property element at target with an object, through the element at by: the
    // Property element itself, or a Reference that names it.
    void add(property_set& set, std::size_t target, std::size_t by, std::string_view owner)
    {
        const std::size_t slot = slots_[target];
        std::optional<property>& found = properties_[slot];
        const bool last_use = --uses_[slot] == 0;
        if (!found) {
            return;
        }
        const error_context context{.element = outline_.path(by), .attribute = by == target ? "id" : "ref"};
        if (set.ids.contains(found->id)) {
            log_.error(errc::duplicate_property_id,
                       "another property of " + std::string(owner) + " has the identifier " + quote(found->id),
                       context);
            return;
        }
        if (!last_use && !charge_copy(*found, context)) {
            return;
        }
        set.ids.insert(found->id);
        if (last_use) {
            set.items.push_back(std::move(*found));
        } else {
            set.items.push_back(*found);
        }
    }

    // A copy of a property counts against the budget, so that a large property named by many References cannot exhaust
    // the memory. False when it is beyond the budget, which is recorded.
    bool charge_copy(const property& item, const error_context& context)
    {
        return budget_.copy(held_size(item), "the property " + quote(item.id), context, log_);
    }

    property_list collect(std::size_t owner)
    {
        const std::string name = owner == no_element ? "the root element" : outline_.path(owner);
        property_set set;
        for_each_child(owner, [&](std::size_t index) {
            if (const std::optional<std::size_t> target = property_of(index)) {
                add(set, *target, index, name);
            }
        });
        return property_list(std::move(set.items));
    }

    // The properties of the unit: those of its Metadata elements, of which there should be exactly one (spec §11.4).
    property_list metadata()
    {
        property_set set;
        std::size_t first = no_element;
        for_each_child(no_element, [&](std::size_t owner) {
            if (outline_.elements[owner].kind != element_kind::metadata) {
                return;
            }
            if (first == no_element) {
                first = owner;
            } else {
                log_.warning(errc::invalid_metadata,
                             "the unit has more than one Metadata element; their properties are all read",
                             {.element = outline_.path(owner)});
            }
            for_each_child(owner, [&](std::size_t index) { add_metadata(set, index); });
        });

        if (first == no_element) {
            log_.warning(errc::invalid_metadata, "the unit has no Metadata element",
                         {.element = "/" + std::string(outline_.root.name())});
        } else {
            for (const std::string_view id : mandatory_metadata) {
                if (!set.ids.contains(std::string(id))) {
                    log_.warning(errc::invalid_metadata, "the Metadata element has no " + std::string(id) + " property",
                                 {.element = outline_.path(first)});
                }
            }
        }
        return property_list(std::move(set.items));
    }

    void add_metadata(property_set& set, std::size_t index)
    {
        const outline_element& element = outline_.elements[index];
        if (element.kind == element_kind::reference && !names_property(element)) {
            if (element.target != no_element) {
                log_.warning(errc::invalid_metadata,
                             "the Reference names " + outline_.path(element.target) +
                                 ", which is not a property, and is ignored",
                             {.element = outline_.path(index), .attribute = "ref"});
            }
            return;
        }
        const std::optional<std::size_t> target = property_of(index);
        if (!target) {
            return;
        }
        if (const std::optional<property>& found = properties_[slots_[*target]]; found && !is_metadata_id(found->id)) {
            log_.warning(errc::invalid_metadata,
                         "the identifier " + quote(found->id) +
                             " is not in the XISF namespace, as those of the properties of the Metadata element are",
                         {.element = outline_.path(index), .attribute = index == *target ? "id" : "ref"});
        }
        add(set, *target, index, "the unit");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // One Property element

    std::optional<property> read_element(std::size_t index, const data_block* block)
    {
        const pugi::xml_node node = outline_.elements[index].node;
        const std::string path = outline_.path(index);
        const pugi::xml_attribute id = node.attribute("id");
        if (id.empty() || *id.value() == '\0') {
            log_.error(errc::invalid_property_id,
                       id.empty() ? "the Property element has no id attribute" : "the property identifier is empty",
                       {.element = path, .attribute = "id"});
            return std::nullopt;
        }
        if (!is_property_id(id.value())) {
            log_.warning(errc::invalid_property_id, quote(id.value()) + " is not a property identifier",
                         {.element = path, .attribute = "id"});
        }

        const pugi::xml_attribute type = node.attribute("type");
        if (type.empty()) {
            log_.error(errc::invalid_property, "the Property element has no type attribute",
                       {.element = path, .attribute = "type"});
            return std::nullopt;
        }
        const std::optional<property_type> named = property_type_named(type.value());
        if (!named) {
            log_.error(errc::unsupported_property_type, quote(type.value()) + " is not a property type",
                       {.element = path, .attribute = "type"});
            return std::nullopt;
        }

        const value_element element{.node = node, .path = path, .block = block, .type = *named};
        std::optional<property_value> value = values_.read(element);
        if (!value) {
            return std::nullopt;
        }
        property result{.id = id.value(), .value = std::move(*value), .comment = node.attribute("comment").value()};
        check_reserved_type(element, result.id);
        result.format = read_format(element);
        return result;
    }

    // The reserved identifiers have the types of the specification (spec §11.4, §11.5.3). Another type is tolerated:
    // PixInsight writes XISF:CreationTime as a String.
    void check_reserved_type(const value_element& element, const std::string& id)
    {
        const std::optional<property_type> reserved = reserved_property_type(id);
        if (reserved && *reserved != element.type) {
            log_.warning(errc::reserved_property_type,
                         "the property " + quote(id) + " is a " + std::string(property_type_name(*reserved)) +
                             " property, but has the type " + std::string(property_type_name(element.type)),
                         {.element = element.path, .attribute = "type"});
        }
    }

    // A format specifier only says how to show the value, so a malformed one is dropped (spec §8.4.3). TimePoint
    // properties have none (spec §8.4.3.1).
    std::optional<property_format> read_format(const value_element& element)
    {
        const pugi::xml_attribute format = element.node.attribute("format");
        if (format.empty()) {
            return std::nullopt;
        }
        const error_context context{.element = element.path, .attribute = "format"};
        if (element.type == property_type::time_point) {
            log_.warning(errc::invalid_format_specifier,
                         "a TimePoint property has no format specifier, so it is ignored", context);
            return std::nullopt;
        }
        try {
            return parse_format_specifier(format.value());
        } catch (const invalid_data_error& failure) {
            log_.warning(failure.code(), std::string(failure.what()) + "; it is ignored", context);
        }
        return std::nullopt;
    }

    const unit_outline& outline_;
    const std::vector<data_block>& blocks_;
    value_reader& values_;
    ancillary_budget& budget_;
    diagnostic_log& log_;
    // The slot of each Property element of the outline in properties_.
    std::vector<std::size_t> slots_;
    // Each Property element as read: its property, or nothing when it is unavailable.
    std::vector<std::optional<property>> properties_{};
    // How many associations each property has left.
    std::vector<std::size_t> uses_{};
};

} // namespace

unit_properties read_properties(const unit_outline& outline, const std::vector<data_block>& blocks,
                                value_reader& values, ancillary_budget& budget, diagnostic_log& log)
{
    return property_reader(outline, blocks, values, budget, log).read();
}

} // namespace openxisf::detail
