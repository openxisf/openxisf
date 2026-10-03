// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "xml/namespaces.h"

#include <ranges>

namespace openxisf::detail {

namespace {

constexpr std::string_view declaration_prefix = "xmlns:";

// Bound to the prefix xml in every document (Namespaces in XML 1.0, §3).
constexpr std::string_view xml_namespace = "http://www.w3.org/XML/1998/namespace";

} // namespace

std::string_view local_name(std::string_view name) noexcept
{
    const std::size_t colon = name.find(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

void namespace_scope::enter(const pugi::xml_node& element)
{
    marks_.push_back(bindings_.size());
    for (const pugi::xml_attribute& attribute : element.attributes()) {
        const std::string_view name = attribute.name();
        const std::string_view uri = attribute.value();
        if (name == "xmlns") {
            // An empty value takes the default namespace out of scope.
            bindings_.push_back({.prefix = {}, .uri = uri});
        } else if (name.starts_with(declaration_prefix) && !uri.empty()) {
            // A prefix cannot be undeclared in XML 1.0, so an empty value declares nothing.
            bindings_.push_back({.prefix = name.substr(declaration_prefix.size()), .uri = uri});
        }
    }
}

void namespace_scope::leave() noexcept
{
    while (bindings_.size() > marks_.back()) {
        bindings_.pop_back();
    }
    marks_.pop_back();
}

std::optional<std::string_view> namespace_scope::namespace_of(std::string_view name) const noexcept
{
    const std::size_t colon = name.find(':');
    const std::string_view prefix = colon == std::string_view::npos ? std::string_view() : name.substr(0, colon);
    if (prefix == "xml") {
        return xml_namespace;
    }
    for (const binding& declared : std::views::reverse(bindings_)) {
        if (declared.prefix == prefix) {
            return declared.uri;
        }
    }
    if (prefix.empty()) {
        return std::string_view();
    }
    return std::nullopt;
}

bool is_xisf_namespace(const std::optional<std::string_view>& uri) noexcept
{
    return uri && (uri->empty() || *uri == xisf_namespace);
}

} // namespace openxisf::detail
