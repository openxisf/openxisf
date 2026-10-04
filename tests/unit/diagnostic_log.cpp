// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "core/diagnostic_log.h"

#include <openxisf/error.h>

#include "support/throws.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

using openxisf::errc;
using openxisf::severity;
using openxisf::detail::diagnostic_log;
using openxisf::detail::throw_unit_error;
using openxisf::test::throws;

TEST(diagnostic_log, records_every_diagnostic_in_order)
{
    diagnostic_log log(false);
    log.info(errc::unknown_element, "an info", {.element = "/xisf/x:Extension[1]"});
    log.warning(errc::invalid_uid, "a warning");
    log.error(errc::duplicate_uid, "an error", {.attribute = "uid"});

    const std::vector<openxisf::diagnostic> diagnostics = log.release();
    ASSERT_EQ(diagnostics.size(), 3U);
    EXPECT_EQ(diagnostics[0].severity, severity::info);
    EXPECT_EQ(diagnostics[0].code, errc::unknown_element);
    EXPECT_EQ(diagnostics[0].message, "an info");
    EXPECT_EQ(diagnostics[0].context.element, "/xisf/x:Extension[1]");
    EXPECT_EQ(diagnostics[1].severity, severity::warning);
    EXPECT_EQ(diagnostics[2].severity, severity::error);
    EXPECT_EQ(diagnostics[2].context.attribute, "uid");
    EXPECT_TRUE(log.entries().empty());
}

TEST(diagnostic_log, an_error_throws_in_strict_mode)
{
    diagnostic_log log(true);
    log.info(errc::unknown_element, "an info");
    log.warning(errc::invalid_uid, "a warning");
    try {
        log.error(errc::duplicate_uid, "an error", {.element = "/xisf/Image[2]"});
        ADD_FAILURE() << "no exception";
    } catch (const openxisf::invalid_data_error& failure) {
        EXPECT_EQ(failure.code(), errc::duplicate_uid);
        EXPECT_STREQ(failure.what(), "an error (element /xisf/Image[2])");
    }
    EXPECT_EQ(log.entries().size(), 2U);
}

TEST(diagnostic_log, the_exception_of_an_error_says_what_kind_of_failure_it_is)
{
    EXPECT_TRUE(throws<openxisf::integrity_error>(errc::checksum_mismatch,
                                                  [] { throw_unit_error(errc::checksum_mismatch, "message", {}); }));
    for (const errc code : {errc::unsupported_version, errc::unsupported_location, errc::unsupported_checksum,
                            errc::unsupported_compression, errc::unsupported_property_type,
                            errc::unsupported_sample_format, errc::unsupported_color_space}) {
        EXPECT_TRUE(throws<openxisf::unsupported_error>(code, [code] { throw_unit_error(code, "message", {}); }));
    }
    for (const errc code : {errc::duplicate_uid, errc::invalid_location, errc::invalid_checksum}) {
        EXPECT_TRUE(throws<openxisf::invalid_data_error>(code, [code] { throw_unit_error(code, "message", {}); }));
    }

    try {
        throw_unit_error(errc::checksum_mismatch, "message", {.element = "/xisf/Image[1]", .offset = 4096});
    } catch (const openxisf::error& failure) {
        EXPECT_STREQ(failure.what(), "message (element /xisf/Image[1], offset 4096)");
    }
}

} // namespace
