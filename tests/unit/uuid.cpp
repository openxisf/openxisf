// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "core/uuid.h"

#include "support/throws.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string_view>

namespace {

using openxisf::errc;
using openxisf::invalid_data_error;
using openxisf::detail::format_uuid;
using openxisf::detail::make_uuid_v4;
using openxisf::detail::parse_uuid;
using openxisf::detail::uuid;
using openxisf::detail::xoshiro256starstar;
using openxisf::test::throws;

constexpr std::string_view spec_example = "c5c93b6d-9072-4e85-9548-1a5391377683";

TEST(uuid, canonical_form_of_the_specification_round_trips)
{
    const uuid id = parse_uuid(spec_example);

    EXPECT_EQ(id[0], std::byte{0xC5});
    EXPECT_EQ(id[15], std::byte{0x83});
    EXPECT_EQ(format_uuid(id), spec_example);
}

TEST(uuid, parsing_accepts_uppercase_and_formatting_writes_lowercase)
{
    EXPECT_EQ(format_uuid(parse_uuid("C5C93B6D-9072-4E85-9548-1A5391377683")), spec_example);
}

TEST(uuid, other_forms_are_invalid)
{
    for (const std::string_view text :
         {"", "c5c93b6d90724e8595481a5391377683", "{c5c93b6d-9072-4e85-9548-1a5391377683}",
          "urn:uuid:c5c93b6d-9072-4e85-9548-1a5391377683", "c5c93b6d9-072-4e85-9548-1a5391377683",
          "c5c93b6d-9072-4e85-9548-1a539137768", "c5c93b6d-9072-4e85-9548-1a53913776833",
          "g5c93b6d-9072-4e85-9548-1a5391377683", " c5c93b6d-9072-4e85-9548-1a5391377683",
          "c5c93b6d-9072-4e85-9548-1a5391377683 ", "c5c93b6d-9072-4e85-9548_1a5391377683",
          "c5c93b6d-9072-4e85-9548-1a539137768\xC3\xA9"}) {
        EXPECT_TRUE(throws<invalid_data_error>(errc::invalid_uuid, [text] { (void)parse_uuid(text); }))
            << testing::PrintToString(text);
    }
}

// From the state {1, 2, 3, 4}, the generator's first two outputs are 0x2D00 and 0 (see xoshiro.cpp's test), so the
// bytes show where each output goes and which bits the version and variant replace.
TEST(uuid, version_4_takes_two_outputs_in_big_endian_order)
{
    xoshiro256starstar generator({1, 2, 3, 4});

    EXPECT_EQ(format_uuid(make_uuid_v4(generator)), "00000000-0000-4d00-8000-000000000000");
}

TEST(uuid, version_4_sets_the_version_and_variant_fields)
{
    xoshiro256starstar generator = xoshiro256starstar::from_random_device();
    for (int i = 0; i < 1000; ++i) {
        const uuid id = make_uuid_v4(generator);
        ASSERT_EQ(id[6] >> 4, std::byte{0x4});
        ASSERT_EQ(id[8] >> 6, std::byte{0x2});
    }
}

} // namespace
