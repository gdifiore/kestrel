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
