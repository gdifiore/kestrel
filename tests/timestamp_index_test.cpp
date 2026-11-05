#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "kestrel/line_index.hpp"
#include "kestrel/timestamp_index.hpp"

#include <string_view>
#include <string>

namespace
{
    std::span<const char> view(std::string_view text)
    {
        return {text.data(), text.size()};
    }
}

TEST_CASE("ISO-8601 parser normalizes timezone offsets to UTC")
{
    const auto utc = kestrel::parse_iso8601(view("2026-08-01T14:00:00Z"));
    CHECK(kestrel::parse_iso8601(view("2026-08-01T10:00:00-04:00")) == utc);
    CHECK(kestrel::parse_iso8601(view("2026-08-01 16:00:00+02:00")) == utc);
}

TEST_CASE("ISO-8601 parser accepts fractional seconds but rejects malformed offsets")
{
    CHECK(kestrel::parse_iso8601(view("2026-08-01T14:00:00.123Z")) ==
          kestrel::parse_iso8601(view("2026-08-01T14:00:00Z")));
    CHECK(kestrel::parse_iso8601(view("2026-08-01T14:00:00+2:00")) == kestrel::TimestampIndex::kNone);
    CHECK(kestrel::parse_iso8601(view("2026-08-01T14:00:00+24:00")) == kestrel::TimestampIndex::kNone);
}

TEST_CASE("progress callback can cancel timestamp indexing")
{
    const std::string text = "2026-08-01T14:00:00Z\n2026-08-01T14:01:00Z\n";
    kestrel::LineIndex lines(view(text));
    CHECK_THROWS_AS(kestrel::TimestampIndex(view(text), lines,
                                             [](std::size_t, std::size_t)
                                             { return false; }),
                    kestrel::LoadCancelled);
}

TEST_CASE("timestamp index preserves complete fractional seconds and timezone suffixes")
{
    const std::string text =
        "2026-11-05T12:00:00.123456789+02:00 positive offset\n"
        "2026-11-05T04:30:00.123456789-05:30 negative offset\n"
        "2026-11-05T10:00:00.123456789123456789Z long fraction\n"
        "2026-11-05T10:00:00.123456789123456789 no timezone\n";
    kestrel::LineIndex lines(view(text));
    kestrel::TimestampIndex timestamps(view(text), lines);
    const auto expected = kestrel::parse_iso8601(view("2026-11-05T10:00:00Z"));
    REQUIRE(lines.line_count() == 4);
    for (std::size_t i = 0; i < lines.line_count(); ++i)
        CHECK(timestamps.at(i) == expected);
    CHECK(timestamps.min_ts() == expected);
    CHECK(timestamps.max_ts() == expected);
}

TEST_CASE("timestamp index rejects malformed suffixes beyond the old prefix limit")
{
    const std::string text =
        "2026-11-05T10:00:00.123456789123456789+24:00 malformed offset\n"
        "2026-11-05T10:00:00.123456789123456789+02\n"
        "2026-11-05T10:00:00Z valid next line\n";
    kestrel::LineIndex lines(view(text));
    kestrel::TimestampIndex timestamps(view(text), lines);
    CHECK(timestamps.at(0) == kestrel::TimestampIndex::kNone);
    CHECK(timestamps.at(1) == kestrel::TimestampIndex::kNone);
    CHECK(timestamps.at(2) == kestrel::parse_iso8601(view("2026-11-05T10:00:00Z")));
}
