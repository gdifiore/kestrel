#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kestrel
{
    class LineIndex;

    // Parse ISO-8601 at the start of `buf`:
    //   YYYY-MM-DD[T ]HH:MM:SS
    // Fractional seconds and timezone suffix are ignored (value treated as UTC).
    // Returns epoch seconds or INT64_MIN on no match.
    int64_t parse_iso8601(std::span<const char> buf);

    class TimestampIndex
    {
    public:
        static constexpr int64_t kNone = INT64_MIN;

        TimestampIndex() = default;
        TimestampIndex(std::span<const char> source, const LineIndex &lines);

        std::size_t size() const noexcept { return ts_.size(); }
        int64_t at(std::size_t line) const
        {
            assert(line < ts_.size());
            return ts_[line];
        }
        bool has(std::size_t line) const
        {
            assert(line < ts_.size());
            return ts_[line] != kNone;
        }

        int64_t min_ts() const noexcept { return min_; }
        int64_t max_ts() const noexcept { return max_; }
        bool empty() const noexcept { return min_ == kNone; }

    private:
        std::vector<int64_t> ts_;
        int64_t min_ = kNone;
        int64_t max_ = kNone;
    };
} // namespace kestrel
