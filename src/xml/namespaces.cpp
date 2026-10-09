// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "xml/namespaces.h"

#include <cstddef>

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
    const auto declare = [this](std::string_view prefix, std::string_view uri) {
        const auto innermost = innermost_.find(prefix);
        const std::size_t hidden = innermost == innermost_.end() ? no_binding : innermost->second;
        bindings_.push_back({.prefix = prefix, .uri = uri, .hidden = hidden});
        innermost_[prefix] = bindings_.size() - 1;
    };
    for (const pugi::xml_attribute& attribute : element.attributes()) {
        const std::string_view name = attribute.name();
        const std::string_view uri = attribute.value();
        if (name == "xmlns") {
            // An empty value takes the default namespace out of scope.
            declare({}, uri);
        } else if (name.starts_with(declaration_prefix) && !uri.empty()) {
            // A prefix cannot be undeclared in XML 1.0, so an empty value declares nothing.
            declare(name.substr(declaration_prefix.size()), uri);
        }
    }
}

void namespace_scope::leave() noexcept
{
    while (bindings_.size() > marks_.back()) {
        const binding& last = bindings_.back();
        if (last.hidden == no_binding) {
            innermost_.erase(last.prefix);
        } else {
            innermost_.find(last.prefix)->second = last.hidden;
        }
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
    if (const auto found = innermost_.find(prefix); found != innermost_.end()) {
        return bindings_[found->second].uri;
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
