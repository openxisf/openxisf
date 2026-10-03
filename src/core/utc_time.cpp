// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 The OpenXISF Authors

#include "core/utc_time.h"

#include <openxisf/error.h>

#include "core/quote.h"
#include "core/text_grammar.h"

#include <optional>
#include <utility>

namespace openxisf::detail {

namespace {

constexpr std::int64_t minutes_per_day = 1440;

bool is_leap_year(std::int64_t year) noexcept
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

unsigned days_in_month(std::int64_t year, unsigned month) noexcept
{
    if (month == 2) {
        return is_leap_year(year) ? 29 : 28;
    }
    return month == 4 || month == 6 || month == 9 || month == 11 ? 30 : 31;
}

// Reads a TimePoint from left to right. Each step returns false when the text does not follow the grammar.
class time_point_scanner
{
public:
    explicit time_point_scanner(std::string_view text) noexcept : text_(text) {}

    [[nodiscard]] bool at_end() const noexcept
    {
        return position_ == text_.size();
    }

    // True, and past c, when c is next.
    bool skip(char c) noexcept
    {
        if (!at_end() && text_[position_] == c) {
            ++position_;
            return true;
        }
        return false;
    }

    // The value of the next count digits.
    std::optional<unsigned> digits(std::size_t count) noexcept
    {
        unsigned value = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (at_end() || text_[position_] < '0' || text_[position_] > '9') {
                return std::nullopt;
            }
            value = (value * 10) + static_cast<unsigned>(text_[position_] - '0');
            ++position_;
        }
        return value;
    }

    // The nanoseconds of a decimal fraction of at least one digit, truncated after the ninth.
    std::optional<std::uint32_t> fraction() noexcept
    {
        std::uint32_t nanoseconds = 0;
        std::size_t count = 0;
        while (!at_end() && text_[position_] >= '0' && text_[position_] <= '9') {
            if (count < 9) {
                nanoseconds = (nanoseconds * 10) + static_cast<std::uint32_t>(text_[position_] - '0');
            }
            ++count;
            ++position_;
        }
        for (std::size_t i = count; i < 9; ++i) {
            nanoseconds *= 10;
        }
        return count == 0 ? std::nullopt : std::optional(nanoseconds);
    }

private:
    std::string_view text_;
    std::size_t position_ = 0;
};

// A TimePoint as written, before it is checked and moved to UTC.
struct written_time_point
{
    date_time local{};
    // Minutes east of UTC.
    int offset = 0;
};

// The date, YYYY-MM-DD.
bool scan_date(time_point_scanner& scan, date_time& local) noexcept
{
    const std::optional<unsigned> year = scan.digits(4);
    if (!year || !scan.skip('-')) {
        return false;
    }
    const std::optional<unsigned> month = scan.digits(2);
    if (!month || !scan.skip('-')) {
        return false;
    }
    const std::optional<unsigned> day = scan.digits(2);
    if (!day) {
        return false;
    }
    local.year = static_cast<int>(*year);
    local.month = *month;
    local.day = *day;
    return true;
}

// The time after the T: hh:mm, then optionally :ss and a fraction.
bool scan_time(time_point_scanner& scan, date_time& local) noexcept
{
    const std::optional<unsigned> hour = scan.digits(2);
    if (!hour || !scan.skip(':')) {
        return false;
    }
    const std::optional<unsigned> minute = scan.digits(2);
    if (!minute) {
        return false;
    }
    local.hour = *hour;
    local.minute = *minute;
    if (!scan.skip(':')) {
        return true;
    }
    const std::optional<unsigned> second = scan.digits(2);
    if (!second) {
        return false;
    }
    local.second = *second;
    if (!scan.skip('.')) {
        return true;
    }
    const std::optional<std::uint32_t> nanoseconds = scan.fraction();
    if (!nanoseconds) {
        return false;
    }
    local.nanosecond = *nanoseconds;
    return true;
}

// The zone after a time: Z, ±hh or ±hh:mm, as minutes east of UTC. No zone is UTC.
bool scan_zone(time_point_scanner& scan, int& offset) noexcept
{
    if (scan.at_end() || scan.skip('Z')) {
        return true;
    }
    const bool negative = scan.skip('-');
    if (!negative && !scan.skip('+')) {
        return false;
    }
    const std::optional<unsigned> hours = scan.digits(2);
    if (!hours || *hours > 23) {
        return false;
    }
    unsigned minutes = 0;
    if (scan.skip(':')) {
        const std::optional<unsigned> written = scan.digits(2);
        if (!written || *written > 59) {
            return false;
        }
        minutes = *written;
    }
    const int magnitude = static_cast<int>((*hours * 60) + minutes);
    offset = negative ? -magnitude : magnitude;
    return true;
}

std::optional<written_time_point> scan_time_point(std::string_view text) noexcept
{
    time_point_scanner scan(text);
    written_time_point result;
    if (!scan_date(scan, result.local)) {
        return std::nullopt;
    }
    if (!scan.at_end() &&
        (!scan.skip('T') || !scan_time(scan, result.local) || !scan_zone(scan, result.offset) || !scan.at_end())) {
        return std::nullopt;
    }
    return result;
}

// True when the fields of a TimePoint as written name a time that exists, 24:00:00 included.
bool is_valid_local_time(const date_time& local) noexcept
{
    const bool end_of_day = local.hour == 24 && local.minute == 0 && local.second == 0 && local.nanosecond == 0;
    return local.month >= 1 && local.month <= 12 && local.day >= 1 &&
           local.day <= days_in_month(local.year, local.month) && (local.hour <= 23 || end_of_day) &&
           local.minute <= 59 && local.second <= 60;
}

// The calendar repeats every 400 years, which have 146097 days. Counting years from March 1 puts the leap day at the
// end of a year, so that the month lengths before it follow a fixed pattern.
constexpr std::int64_t days_per_era = 146097;
// Days from 0000-03-01 to 1970-01-01.
constexpr std::int64_t epoch_offset = 719468;

// The number of days from March 1 to the first day of a month, counting months from March (0) to February (11).
std::int64_t days_before_month(std::int64_t month_from_march) noexcept
{
    return ((153 * month_from_march) + 2) / 5;
}

// Appends value in decimal, with leading zeros up to width digits.
void append_padded(std::string& text, std::int64_t value, int width)
{
    const std::string digits = std::to_string(value);
    if (std::cmp_less(digits.size(), width)) {
        text.append(static_cast<std::size_t>(width) - digits.size(), '0');
    }
    text += digits;
}

} // namespace

std::int64_t days_from_civil(const civil_date& date) noexcept
{
    const std::int64_t year = date.month <= 2 ? date.year - 1 : date.year;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const std::int64_t year_of_era = year - (era * 400);
    const std::int64_t month_from_march = date.month > 2 ? date.month - 3 : date.month + 9;
    const std::int64_t day_of_year = days_before_month(month_from_march) + date.day - 1;
    const std::int64_t day_of_era = (year_of_era * 365) + (year_of_era / 4) - (year_of_era / 100) + day_of_year;
    return (era * days_per_era) + day_of_era - epoch_offset;
}

civil_date civil_from_days(std::int64_t days) noexcept
{
    const std::int64_t shifted = days + epoch_offset;
    const std::int64_t era = (shifted >= 0 ? shifted : shifted - (days_per_era - 1)) / days_per_era;
    const std::int64_t day_of_era = shifted - (era * days_per_era);
    // Removes the leap days before day_of_era: one every 4 years (1460 days), none every 100 years (36524 days), and
    // one every 400 years (the last day of the era).
    const std::int64_t year_of_era =
        (day_of_era - (day_of_era / 1460) + (day_of_era / 36524) - (day_of_era / (days_per_era - 1))) / 365;
    const std::int64_t day_of_year = day_of_era - ((year_of_era * 365) + (year_of_era / 4) - (year_of_era / 100));
    const std::int64_t month_from_march = ((5 * day_of_year) + 2) / 153;
    const std::int64_t month = month_from_march < 10 ? month_from_march + 3 : month_from_march - 9;
    return {.year = year_of_era + (era * 400) + (month <= 2 ? 1 : 0),
            .month = static_cast<unsigned>(month),
            .day = static_cast<unsigned>(day_of_year - days_before_month(month_from_march) + 1)};
}

std::string format_utc_time(std::chrono::sys_time<std::chrono::nanoseconds> time)
{
    constexpr std::int64_t nanoseconds_per_second = 1'000'000'000;
    constexpr std::int64_t seconds_per_day = 86'400;

    // Floor divisions, so that times before 1970 fall into the right second and day.
    std::int64_t seconds = time.time_since_epoch().count() / nanoseconds_per_second;
    std::int64_t nanoseconds = time.time_since_epoch().count() % nanoseconds_per_second;
    if (nanoseconds < 0) {
        nanoseconds += nanoseconds_per_second;
        --seconds;
    }
    std::int64_t days = seconds / seconds_per_day;
    std::int64_t second_of_day = seconds % seconds_per_day;
    if (second_of_day < 0) {
        second_of_day += seconds_per_day;
        --days;
    }

    // A 64-bit count of nanoseconds spans the years 1677 to 2262, so the year always has four digits.
    const civil_date date = civil_from_days(days);
    return format_time_point({.year = static_cast<int>(date.year),
                              .month = date.month,
                              .day = date.day,
                              .hour = static_cast<unsigned>(second_of_day / 3600),
                              .minute = static_cast<unsigned>(second_of_day / 60 % 60),
                              .second = static_cast<unsigned>(second_of_day % 60),
                              .nanosecond = static_cast<std::uint32_t>(nanoseconds)});
}

date_time parse_time_point(std::string_view text)
{
    const std::optional<written_time_point> written = scan_time_point(trim_white_space(text));
    if (!written || !is_valid_local_time(written->local)) {
        throw invalid_data_error(errc::invalid_time_point, quote(text) + " is not an ISO 8601 date and time");
    }

    // The same instant in UTC. A leap second keeps its second: only the minutes move.
    const date_time& local = written->local;
    std::int64_t days = days_from_civil({.year = local.year, .month = local.month, .day = local.day});
    std::int64_t minute_of_day = (std::int64_t{local.hour} * 60) + local.minute - written->offset;
    if (minute_of_day < 0) {
        minute_of_day += minutes_per_day;
        --days;
    } else if (minute_of_day >= minutes_per_day) {
        minute_of_day -= minutes_per_day;
        ++days;
    }
    const civil_date date = civil_from_days(days);
    return {.year = static_cast<int>(date.year),
            .month = date.month,
            .day = date.day,
            .hour = static_cast<unsigned>(minute_of_day / 60),
            .minute = static_cast<unsigned>(minute_of_day % 60),
            .second = local.second,
            .nanosecond = local.nanosecond};
}

bool is_valid_time_point(const date_time& time) noexcept
{
    return time.year >= 0 && time.year <= 9999 && is_valid_local_time(time) && time.hour <= 23 &&
           time.nanosecond <= 999'999'999;
}

std::string format_time_point(const date_time& time)
{
    std::string text;
    append_padded(text, time.year, 4);
    text += '-';
    append_padded(text, time.month, 2);
    text += '-';
    append_padded(text, time.day, 2);
    text += 'T';
    append_padded(text, time.hour, 2);
    text += ':';
    append_padded(text, time.minute, 2);
    text += ':';
    append_padded(text, time.second, 2);
    if (time.nanosecond != 0) {
        std::string fraction;
        append_padded(fraction, time.nanosecond, 9);
        text += '.';
        text += fraction.substr(0, fraction.find_last_not_of('0') + 1);
    }
    text += 'Z';
    return text;
}

} // namespace openxisf::detail
