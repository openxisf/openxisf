// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "xml/namespaces.h"

#include "xml/xml_document.h"

#include <gtest/gtest.h>

#include <optional>
#include <string_view>

namespace {

using namespace std::string_view_literals;
using openxisf::detail::is_xisf_namespace;
using openxisf::detail::local_name;
using openxisf::detail::namespace_scope;
using openxisf::detail::parse_xml;

TEST(namespaces, the_local_name_follows_the_prefix)
{
    EXPECT_EQ(local_name("Image"), "Image");
    EXPECT_EQ(local_name("x:Image"), "Image");
}

TEST(namespaces, names_take_the_namespaces_declared_by_their_ancestors)
{
    const auto document = parse_xml(R"(<r xmlns="urn:d" xmlns:p="urn:p"><c xmlns="" xmlns:p="urn:q"/></r>)", 0, {});
    const pugi::xml_node r = document->document_element();
    namespace_scope scope;

    EXPECT_EQ(scope.namespace_of("e"), ""sv);
    scope.enter(r);
    EXPECT_EQ(scope.namespace_of("e"), "urn:d"sv);
    EXPECT_EQ(scope.namespace_of("p:e"), "urn:p"sv);

    // A declaration of an element replaces that of an ancestor, and xmlns="" takes the default namespace away.
    scope.enter(r.first_child());
    EXPECT_EQ(scope.namespace_of("e"), ""sv);
    EXPECT_EQ(scope.namespace_of("p:e"), "urn:q"sv);

    scope.leave();
    EXPECT_EQ(scope.namespace_of("e"), "urn:d"sv);
    EXPECT_EQ(scope.namespace_of("p:e"), "urn:p"sv);
    scope.leave();
    EXPECT_EQ(scope.namespace_of("p:e"), std::nullopt);
}

TEST(namespaces, a_declaration_hides_those_of_its_prefix_until_its_element_is_left)
{
    const auto document = parse_xml(
        R"(<a xmlns:p="urn:1"><b xmlns:q="urn:q" xmlns:p="urn:2"><c xmlns:p="urn:3" xmlns:q="urn:r"/></b></a>)", 0, {});
    const pugi::xml_node a = document->document_element();
    namespace_scope scope;

    scope.enter(a);
    scope.enter(a.first_child());
    scope.enter(a.first_child().first_child());
    EXPECT_EQ(scope.namespace_of("p:e"), "urn:3"sv);
    EXPECT_EQ(scope.namespace_of("q:e"), "urn:r"sv);
    scope.leave();
    EXPECT_EQ(scope.namespace_of("p:e"), "urn:2"sv);
    EXPECT_EQ(scope.namespace_of("q:e"), "urn:q"sv);
    scope.leave();
    EXPECT_EQ(scope.namespace_of("p:e"), "urn:1"sv);
    EXPECT_EQ(scope.namespace_of("q:e"), std::nullopt);
    scope.leave();
    EXPECT_EQ(scope.namespace_of("p:e"), std::nullopt);
}

TEST(namespaces, an_undeclared_prefix_has_no_namespace_but_xml_always_has_one)
{
    const auto document = parse_xml(R"(<r xmlns:p=""/>)", 0, {});
    namespace_scope scope;
    scope.enter(document->document_element());

    // A prefix cannot be undeclared in XML 1.0, so the empty declaration declares nothing.
    EXPECT_EQ(scope.namespace_of("p:e"), std::nullopt);
    EXPECT_EQ(scope.namespace_of("q:e"), std::nullopt);
    EXPECT_EQ(scope.namespace_of("xml:e"), "http://www.w3.org/XML/1998/namespace"sv);
}

TEST(namespaces, xisf_elements_are_in_the_xisf_namespace_or_in_none)
{
    EXPECT_TRUE(is_xisf_namespace("http://www.pixinsight.com/xisf"sv));
    EXPECT_TRUE(is_xisf_namespace(""sv));
    EXPECT_FALSE(is_xisf_namespace("http://www.pixinsight.com/xisf/"sv));
    EXPECT_FALSE(is_xisf_namespace("urn:example"sv));
    EXPECT_FALSE(is_xisf_namespace(std::nullopt));
}

} // namespace
