// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/property.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

// Dates of the proleptic Gregorian calendar, and UTC time points written as TimePoint values (spec §8.4.4.4), such as
// XISF:CreationTime. The calendar arithmetic is written out, so that no non-reentrant C function, no time zone database
// and no locale is involved.

namespace openxisf::detail {

/// A date of the proleptic Gregorian calendar. Year 0 is 1 BC.
struct civil_date
{
    std::int64_t year = 1970;
    unsigned month = 1; ///< 1 to 12
    unsigned day = 1;   ///< 1 to the length of the month

    friend bool operator==(const civil_date&, const civil_date&) = default;
};

/// The number of days from 1970-01-01 to date, which must be a valid date. Exact for years of up to nine digits.
[[nodiscard]] std::int64_t days_from_civil(const civil_date& date) noexcept;

/// The date that is the given number of days after 1970-01-01. Exact for years of up to nine digits.
[[nodiscard]] civil_date civil_from_days(std::int64_t days) noexcept;

/// time in UTC, in the ISO 8601 extended format of TimePoint values: 2026-10-02T18:52:31Z. A nonzero fraction of a
/// second follows the seconds without trailing zeros, as in 2015-01-23T19:52:31.46Z.
[[nodiscard]] std::string format_utc_time(std::chrono::sys_time<std::chrono::nanoseconds> time);

/// Parses a TimePoint value (spec §8.4.4.4): an ISO 8601 date, YYYY-MM-DD, optionally followed by T and a time,
/// hh:mm, hh:mm:ss or hh:mm:ss.fraction, and a zone, Z, ±hh or ±hh:mm; leading and trailing white space is ignored.
/// A time without a zone is in UTC, and 24:00:00 is the end of the day. The result is the same instant in UTC, so its
/// year may be -1 or 10000; the fraction is truncated to nanoseconds. Throws invalid_data_error with
/// errc::invalid_time_point.
[[nodiscard]] date_time parse_time_point(std::string_view text);

/// True when every member of time is in its range, and its year is from 0 to 9999, which TimePoint values can hold.
[[nodiscard]] bool is_valid_time_point(const date_time& time) noexcept;

/// The TimePoint representation of time, which must be valid: as format_utc_time() writes it.
[[nodiscard]] std::string format_time_point(const date_time& time);

} // namespace openxisf::detail
