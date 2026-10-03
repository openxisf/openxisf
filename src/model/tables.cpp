// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "model/tables.h"

#include <openxisf/error.h>

#include "core/quote.h"
#include "core/text_grammar.h"
#include "model/format_specifier.h"
#include "model/property_text.h"
#include "model/property_types.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace openxisf::detail {

namespace {

// The bytes that a copy of the fields of a structure holds.
std::uint64_t structure_size(const std::vector<table_field>& fields)
{
    std::uint64_t total = fields.size() * sizeof(table_field);
    for (const table_field& field : fields) {
        total += field.id.size() + field.header.size() + (field.format ? field.format->unit.size() : 0);
    }
    return total;
}

class table_reader
{
public:
    table_reader(const unit_outline& outline, const std::vector<data_block>& blocks, value_reader& values,
                 ancillary_budget& budget, diagnostic_log& log)
        : outline_(outline), values_(values), budget_(budget), log_(log)
    {
        for (const data_block& block : blocks) {
            if (outline.elements[block.element].kind == element_kind::cell) {
                block_of_.emplace(block.element, &block);
            }
        }
    }

    std::unordered_map<std::size_t, table> read()
    {
        for (std::size_t index = 0; index < outline_.elements.size(); ++index) {
            const outline_element& element = outline_.elements[index];
            if (element.kind == element_kind::structure && element.parent == no_element &&
                element.node.attribute("uid").empty()) {
                log_.warning(errc::invalid_table,
                             "a standalone Structure element needs a uid, so that tables can name it",
                             {.element = outline_.path(index)});
            }
        }
        std::unordered_map<std::size_t, table> tables;
        for (std::size_t index = 0; index < outline_.elements.size(); ++index) {
            if (outline_.elements[index].kind == element_kind::table) {
                if (std::optional<table> read = read_table(index)) {
                    tables.emplace(index, std::move(*read));
                }
            }
        }
        return tables;
    }

private:
    // The elements that owner contains directly, in document order.
    [[nodiscard]] std::vector<std::size_t> children(std::size_t owner) const
    {
        std::vector<std::size_t> found;
        for (std::size_t index = owner + 1; index < outline_.elements[owner].end; ++index) {
            if (outline_.elements[index].parent == owner) {
                found.push_back(index);
            }
        }
        return found;
    }

    [[nodiscard]] std::vector<std::size_t> children_of_kind(std::size_t owner, element_kind kind) const
    {
        std::vector<std::size_t> found = children(owner);
        std::erase_if(found, [this, kind](std::size_t index) { return outline_.elements[index].kind != kind; });
        return found;
    }

    bool fail(const std::string& message, error_context context)
    {
        log_.error(errc::invalid_table, message, std::move(context));
        return false;
    }

    std::optional<table> read_table(std::size_t index)
    {
        const pugi::xml_node node = outline_.elements[index].node;
        const std::string path = outline_.path(index);
        const pugi::xml_attribute id = node.attribute("id");
        if (id.empty() || *id.value() == '\0') {
            log_.error(errc::invalid_property_id,
                       id.empty() ? "the Table element has no id attribute" : "the table identifier is empty",
                       {.element = path, .attribute = "id"});
            return std::nullopt;
        }
        if (!is_property_id(id.value())) {
            log_.warning(errc::invalid_property_id, quote(id.value()) + " is not a property identifier",
                         {.element = path, .attribute = "id"});
        }
        table result{.id = id.value(),
                     .caption = node.attribute("caption").value(),
                     .comment = node.attribute("comment").value()};
        std::optional<std::vector<table_field>> fields = structure_of(index, path);
        if (!fields) {
            return std::nullopt;
        }
        result.fields = std::move(*fields);
        if (!read_rows(index, result) || !check_counts(node, path, result)) {
            return std::nullopt;
        }
        return result;
    }

    // Spec §11.3: a table has either one Structure element or one Reference to a standalone Structure element.
    std::optional<std::vector<table_field>> structure_of(std::size_t index, const std::string& path)
    {
        const std::vector<std::size_t> structures = children_of_kind(index, element_kind::structure);
        const std::vector<std::size_t> references = children_of_kind(index, element_kind::reference);
        if (structures.size() + references.size() != 1) {
            fail(structures.empty() && references.empty()
                     ? "the table has no Structure element, and no Reference to one"
                     : "the table has more than one Structure element or Reference",
                 {.element = path});
            return std::nullopt;
        }
        if (!structures.empty()) {
            return read_structure(structures.front());
        }
        const std::size_t reference = references.front();
        const std::size_t target = outline_.elements[reference].target;
        if (target == no_element) {
            // The Reference names nothing, which build_outline() reported.
            return std::nullopt;
        }
        const error_context context{.element = outline_.path(reference), .attribute = "ref"};
        if (outline_.elements[target].kind != element_kind::structure ||
            outline_.elements[target].parent != no_element) {
            log_.error(errc::invalid_reference,
                       "a Table names its structure with a Reference to a standalone Structure element, and " +
                           outline_.path(target) + " is not one",
                       context);
            return std::nullopt;
        }
        // A standalone structure is read once, and copied into each table that names it.
        auto found = standalone_.find(target);
        if (found == standalone_.end()) {
            found = standalone_.emplace(target, read_structure(target)).first;
        }
        if (!found->second ||
            !budget_.copy(structure_size(*found->second), "the structure of " + outline_.path(target), context, log_)) {
            return std::nullopt;
        }
        return found->second;
    }

    // Spec §11.2: at least one field, each identifier once.
    std::optional<std::vector<table_field>> read_structure(std::size_t index)
    {
        const std::string path = outline_.path(index);
        const std::vector<std::size_t> elements = children_of_kind(index, element_kind::field);
        if (elements.empty()) {
            fail("a Structure element has at least one Field element", {.element = path});
            return std::nullopt;
        }
        std::vector<table_field> fields;
        std::unordered_set<std::string> ids;
        for (const std::size_t field : elements) {
            std::optional<table_field> read = read_field(field);
            if (!read) {
                return std::nullopt;
            }
            if (!ids.insert(read->id).second) {
                fail("another field of the structure has the identifier " + quote(read->id),
                     {.element = outline_.path(field), .attribute = "id"});
                return std::nullopt;
            }
            fields.push_back(std::move(*read));
        }
        return fields;
    }

    std::optional<table_field> read_field(std::size_t index)
    {
        const pugi::xml_node node = outline_.elements[index].node;
        const std::string path = outline_.path(index);
        const pugi::xml_attribute id = node.attribute("id");
        if (id.empty() || *id.value() == '\0') {
            log_.error(errc::invalid_property_id,
                       id.empty() ? "the Field element has no id attribute" : "the field identifier is empty",
                       {.element = path, .attribute = "id"});
            return std::nullopt;
        }
        if (!is_property_id(id.value())) {
            log_.warning(errc::invalid_property_id, quote(id.value()) + " is not a property identifier",
                         {.element = path, .attribute = "id"});
        }
        const pugi::xml_attribute type = node.attribute("type");
        if (type.empty()) {
            fail("the Field element has no type attribute", {.element = path, .attribute = "type"});
            return std::nullopt;
        }
        // A field is not of table type (spec §8.4.4.7), and no property type is named Table.
        const std::optional<property_type> named = property_type_named(type.value());
        if (!named) {
            log_.error(errc::unsupported_property_type, quote(type.value()) + " is not a property type",
                       {.element = path, .attribute = "type"});
            return std::nullopt;
        }
        table_field field{.id = id.value(), .type = *named, .header = node.attribute("header").value()};
        field.format = read_format(node, path, *named);
        return field;
    }

    // As for properties, a malformed format specifier is dropped, and a TimePoint has none (spec §8.4.3).
    std::optional<property_format> read_format(const pugi::xml_node& node, const std::string& path, property_type type)
    {
        const pugi::xml_attribute format = node.attribute("format");
        if (format.empty()) {
            return std::nullopt;
        }
        const error_context context{.element = path, .attribute = "format"};
        if (type == property_type::time_point) {
            log_.warning(errc::invalid_format_specifier, "a TimePoint field has no format specifier, so it is ignored",
                         context);
            return std::nullopt;
        }
        try {
            return parse_format_specifier(format.value());
        } catch (const invalid_data_error& failure) {
            log_.warning(failure.code(), std::string(failure.what()) + "; it is ignored", context);
        }
        return std::nullopt;
    }

    // Spec §11.3.3: each Row has one Cell for each field, which serializes a value of its type as a Property element
    // does.
    bool read_rows(std::size_t index, table& result)
    {
        for (const std::size_t row : children_of_kind(index, element_kind::row)) {
            const std::vector<std::size_t> cells = children_of_kind(row, element_kind::cell);
            if (cells.size() != result.fields.size()) {
                return fail("the row has " + std::to_string(cells.size()) + " cells, and the structure has " +
                                std::to_string(result.fields.size()) + " fields",
                            {.element = outline_.path(row)});
            }
            std::vector<property_value> values;
            values.reserve(cells.size());
            for (std::size_t column = 0; column < cells.size(); ++column) {
                std::optional<property_value> value = read_cell(cells[column], result.fields[column].type);
                if (!value) {
                    return false;
                }
                values.push_back(std::move(*value));
            }
            result.rows.push_back(std::move(values));
        }
        return true;
    }

    std::optional<property_value> read_cell(std::size_t index, property_type type)
    {
        const pugi::xml_node node = outline_.elements[index].node;
        const std::string path = outline_.path(index);
        for (const char* attribute : {"id", "type", "format"}) {
            if (!node.attribute(attribute).empty()) {
                log_.warning(errc::invalid_table,
                             "a Cell element has no " + std::string(attribute) + " attribute, so it is ignored",
                             {.element = path, .attribute = attribute});
            }
        }
        const auto block = block_of_.find(index);
        return values_.read(
            {.node = node, .path = path, .block = block == block_of_.end() ? nullptr : block->second, .type = type});
    }

    // Spec §11.3.2: rows and columns attributes, when present, give the size of the table.
    bool check_counts(const pugi::xml_node& node, const std::string& path, const table& result)
    {
        return check_count(node, path, "rows", result.rows.size()) &&
               check_count(node, path, "columns", result.fields.size());
    }

    bool check_count(const pugi::xml_node& node, const std::string& path, const char* name, std::size_t actual)
    {
        const pugi::xml_attribute attribute = node.attribute(name);
        if (attribute.empty()) {
            return true;
        }
        const error_context context{.element = path, .attribute = name};
        std::uint64_t count = 0;
        try {
            count = parse_integer<std::uint64_t>(attribute.value());
        } catch (const invalid_data_error& failure) {
            log_.error(failure.code(), failure.what(), context);
            return false;
        }
        if (count != actual) {
            return fail("the table says it has " + std::to_string(count) + " " + name + ", and it has " +
                            std::to_string(actual),
                        context);
        }
        return true;
    }

    const unit_outline& outline_;
    value_reader& values_;
    ancillary_budget& budget_;
    diagnostic_log& log_;
    // The data block of each Cell element that has one.
    std::unordered_map<std::size_t, const data_block*> block_of_{};
    // The fields of each standalone Structure element read so far; nothing when it cannot be read.
    std::unordered_map<std::size_t, std::optional<std::vector<table_field>>> standalone_{};
};

} // namespace

std::unordered_map<std::size_t, table> read_tables(const unit_outline& outline, const std::vector<data_block>& blocks,
                                                   value_reader& values, ancillary_budget& budget, diagnostic_log& log)
{
    return table_reader(outline, blocks, values, budget, log).read();
}

} // namespace openxisf::detail
