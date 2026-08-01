#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "kestrel/line_index.hpp"
#include "kestrel/timestamp_index.hpp"

#include <string_view>

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
