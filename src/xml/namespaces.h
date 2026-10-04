// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <pugixml.hpp>

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

// Namespaces in XML 1.0, which pugixml leaves to its users: the namespace of an element follows from the declarations
// of its ancestors and its own.

namespace openxisf::detail {

/// The namespace of the XISF core elements (spec §9.5).
inline constexpr std::string_view xisf_namespace = "http://www.pixinsight.com/xisf";

/// The namespace of XML signatures, which a signed header uses (spec §9.5).
inline constexpr std::string_view xml_signature_namespace = "http://www.w3.org/2000/09/xmldsig#";

/// The local part of a qualified name: what follows its colon, or the whole name.
[[nodiscard]] std::string_view local_name(std::string_view name) noexcept;

/// The namespace declarations in scope at an element. The views point into the document, which must outlive the scope.
class namespace_scope
{
public:
    /// Brings the declarations of element into scope. Each enter() is undone by a leave().
    void enter(const pugi::xml_node& element);

    /// Takes the declarations of the element entered last out of scope.
    void leave() noexcept;

    /// The namespace of an element name in this scope: empty when the name has none, and std::nullopt when its prefix
    /// is not declared.
    [[nodiscard]] std::optional<std::string_view> namespace_of(std::string_view name) const noexcept;

private:
    struct binding
    {
        std::string_view prefix;
        std::string_view uri;
    };

    std::vector<binding> bindings_;
    std::vector<std::size_t> marks_;
};

/// True when an element with this namespace is an XISF element: one in the XISF namespace, or, for compatibility, one
/// in no namespace.
[[nodiscard]] bool is_xisf_namespace(const std::optional<std::string_view>& uri) noexcept;

} // namespace openxisf::detail
