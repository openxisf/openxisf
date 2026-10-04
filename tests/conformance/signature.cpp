// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Spec §9.5: a detached XML signature after the root element, on constructed units. Signatures are read, never
// verified.

#include <openxisf/error.h>
#include <openxisf/reader.h>

#include "core/diagnostic_log.h"
#include "model/header.h"
#include "support/diagnostics.h"
#include "support/fixture_builder.h"
#include "support/throws.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

namespace {

using openxisf::errc;
using openxisf::reader;
using openxisf::severity;
using openxisf::signature_status;
using openxisf::test::header_xml;
using openxisf::test::open_header;
using openxisf::test::single_diagnostic;

// The form of the example of spec §9.5, shortened.
constexpr std::string_view signature = R"(<Signature xmlns="http://www.w3.org/2000/09/xmldsig#">
<SignedInfo>
   <CanonicalizationMethod Algorithm="http://www.w3.org/2006/12/xml-c14n11"/>
   <SignatureMethod Algorithm="http://www.w3.org/2000/09/xmldsig#rsa-sha1"/>
   <Reference URI="#XISFRootElement">
      <DigestMethod Algorithm="http://www.w3.org/2000/09/xmldsig#sha1"/>
      <DigestValue>bm90IGEgZGlnZXN0</DigestValue>
   </Reference>
</SignedInfo>
<SignatureValue>bm90IGEgc2lnbmF0dXJl</SignatureValue>
</Signature>)";

// The signature as parse_header() isolates it from a header.
std::optional<std::string> isolated_signature(std::string_view header)
{
    openxisf::detail::diagnostic_log log(false);
    return openxisf::detail::parse_header(header, 16, {}, log).signature;
}

TEST(conformance_signature, a_signed_unit_is_read_and_its_signature_is_not_verified)
{
    const reader file = open_header(header_xml() + "\n" + std::string(signature));

    EXPECT_EQ(file.signature(), signature_status::not_verified);
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::info, errc::signature_not_verified, "/Signature[1]"));
}

TEST(conformance_signature, the_signature_is_kept_exactly_as_written)
{
    EXPECT_EQ(isolated_signature(header_xml() + "\n" + std::string(signature)), signature);

    // Markup in comments, CDATA sections, processing instructions and attribute values does not end the signature,
    // and what follows it is not part of it.
    const std::string tricky = R"(<ds:Signature xmlns:ds="http://www.w3.org/2000/09/xmldsig#" a="/>" b='>'>)"
                               R"(<!-- > </ds:Signature> --><ds:SignatureValue><![CDATA[ > </ds:Signature>]]>)"
                               R"(</ds:SignatureValue><?pi /> </ds:Signature>?><ds:Object/>)"
                               "\r\n\t</ds:Signature>";
    EXPECT_EQ(isolated_signature(header_xml() + tricky + "\n<!-- after -->\n"), tricky);
}

TEST(conformance_signature, an_empty_signature_is_a_signature)
{
    // What a signature holds matters only to a verification, which OpenXISF does not do.
    const reader file = open_header(header_xml() + R"(<Signature xmlns="http://www.w3.org/2000/09/xmldsig#"/>)");
    EXPECT_EQ(file.signature(), signature_status::not_verified);
}

TEST(conformance_signature, a_signature_in_another_namespace_is_not_a_signature)
{
    const reader file = open_header(header_xml() + R"(<Signature xmlns="urn:example"/>)");

    EXPECT_EQ(file.signature(), signature_status::none);
    EXPECT_TRUE(single_diagnostic(file.diagnostics(), severity::warning, errc::unknown_element, "/Signature[1]"));
}

TEST(conformance_signature, only_the_first_signature_counts)
{
    const std::string header = header_xml() + std::string(signature) + std::string(signature);

    const reader file = open_header(header);
    EXPECT_EQ(file.signature(), signature_status::not_verified);
    ASSERT_EQ(file.diagnostics().size(), 2U);
    EXPECT_EQ(file.diagnostics()[1].severity, severity::warning);
    EXPECT_EQ(file.diagnostics()[1].code, errc::unknown_element);
    EXPECT_EQ(file.diagnostics()[1].context.element, "/Signature[2]");
    EXPECT_EQ(isolated_signature(header), signature);
}

TEST(conformance_signature, a_signature_before_the_root_element_takes_its_place)
{
    const std::string header = header_xml();
    const std::string misplaced = header.substr(0, openxisf::test::xml_declaration.size()) + std::string(signature) +
                                  header.substr(openxisf::test::xml_declaration.size());
    EXPECT_TRUE(openxisf::test::throws<openxisf::invalid_data_error>(errc::invalid_root_element,
                                                                     [&misplaced] { (void)open_header(misplaced); }));
}

} // namespace
