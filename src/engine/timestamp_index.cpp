#include "kestrel/timestamp_index.hpp"
#include "kestrel/line_index.hpp"

#include <algorithm>
#include <climits>
#include <ctime>

namespace kestrel
{
    int64_t parse_iso8601(std::span<const char> b)
    {
        if (b.size() < 19)
            return INT64_MIN;

        auto is_digit = [](char c)
        { return c >= '0' && c <= '9'; };
        for (int i : {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18})
            if (!is_digit(b[i]))
                return INT64_MIN;
        if (b[4] != '-' || b[7] != '-')
            return INT64_MIN;
        if (b[10] != 'T' && b[10] != ' ')
            return INT64_MIN;
        if (b[13] != ':' || b[16] != ':')
            return INT64_MIN;

        auto d = [&](size_t i)
        { return b[i] - '0'; };
        int year = d(0) * 1000 + d(1) * 100 + d(2) * 10 + d(3);
        int month = d(5) * 10 + d(6);
        int day = d(8) * 10 + d(9);
        int hour = d(11) * 10 + d(12);
        int minute = d(14) * 10 + d(15);
        int sec = d(17) * 10 + d(18);

        if (month < 1 || month > 12 || day < 1 || day > 31 ||
            hour > 23 || minute > 59 || sec > 60)
            return INT64_MIN;

        static constexpr int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        int max_day = days_in_month[month - 1];
        if (month == 2)
        {
            bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
            if (leap)
                max_day = 29;
        }
        if (day > max_day)
            return INT64_MIN;

        // Fractional seconds do not affect the index's second precision, but
        // consume them before looking for an optional timezone suffix.
        size_t pos = 19;
        if (pos < b.size() && b[pos] == '.')
        {
            ++pos;
            const size_t fraction_start = pos;
            while (pos < b.size() && is_digit(b[pos]))
                ++pos;
            if (pos == fraction_start)
                return INT64_MIN;
        }

        int offset_seconds = 0;
        if (pos < b.size() && b[pos] == 'Z')
        {
            ++pos;
        }
        else if (pos < b.size() && (b[pos] == '+' || b[pos] == '-'))
        {
            const bool negative = b[pos] == '-';
            if (pos + 6 > b.size() || !is_digit(b[pos + 1]) ||
                !is_digit(b[pos + 2]) || b[pos + 3] != ':' ||
                !is_digit(b[pos + 4]) || !is_digit(b[pos + 5]))
                return INT64_MIN;
            const int offset_hour = d(pos + 1) * 10 + d(pos + 2);
            const int offset_minute = d(pos + 4) * 10 + d(pos + 5);
            if (offset_hour > 23 || offset_minute > 59)
                return INT64_MIN;
            offset_seconds = offset_hour * 3600 + offset_minute * 60;
            if (negative)
                offset_seconds = -offset_seconds;
        }

        struct tm t{};
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min = minute;
        t.tm_sec = sec;
        time_t r = timegm(&t);
        if (r == (time_t)-1)
            return INT64_MIN;
        return static_cast<int64_t>(r) - offset_seconds;
    }

    TimestampIndex::TimestampIndex(std::span<const char> src, const LineIndex &li,
                                   ProgressCallback progress)
    {
        const size_t n = li.line_count();
        ts_.resize(n, kNone);
        for (size_t i = 0; i < n; i++)
        {
            if (progress && (i % 8192 == 0) && !progress(i, n))
                throw LoadCancelled();
            size_t s = li.line_start(i);
            size_t e = (i + 1 < n) ? li.line_start(i + 1) : src.size();
            size_t len = std::min<size_t>(e - s, 30);
            int64_t t = parse_iso8601(std::span<const char>(src.data() + s, len));
            ts_[i] = t;
            if (t != kNone)
            {
                if (min_ == kNone || t < min_)
                    min_ = t;
                if (max_ == kNone || t > max_)
                    max_ = t;
            }
        }
        if (progress)
            progress(n, n);
    }
} // namespace kestrel
