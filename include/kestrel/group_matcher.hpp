#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Forward-declare PCRE2 types to keep pcre2.h out of the public header.
struct pcre2_real_code_8;
struct pcre2_real_match_data_8;

namespace kestrel
{

    // Secondary regex matcher for capture-group highlighting. Hyperscan gives
    // match spans but not groups; this re-matches each span with PCRE2 to
    // extract group boundaries. PCRE2 syntax aligns with Hyperscan (both
    // PCRE-flavored), so a pattern that compiles in Hyperscan should compile
    // here too; if it doesn't, compile() returns nullopt and the UI falls
    // back to plain match highlighting.
    //
    // Thread-safety: not thread-safe. Keep on the UI thread; the internal
    // match-data block is reused across calls.
    class GroupMatcher
    {
    public:
        struct Span
        {
            std::size_t start;
            std::size_t end;
        };

        // hs_flags: HS_FLAG_CASELESS / HS_FLAG_DOTALL / HS_FLAG_MULTILINE bits,
        // translated to the PCRE2 equivalents internally.
        static std::optional<GroupMatcher> compile(std::string_view pattern, unsigned hs_flags);

        ~GroupMatcher();
        GroupMatcher(GroupMatcher &&other) noexcept;
        GroupMatcher &operator=(GroupMatcher &&other) noexcept;
        GroupMatcher(const GroupMatcher &) = delete;
        GroupMatcher &operator=(const GroupMatcher &) = delete;

        // Number of capture groups (excluding group 0, the whole match).
        int group_count() const noexcept { return group_count_; }

        // Anchored match of [match_start, match_end) within `buf`. On success,
        // appends matched groups 1..N to `out_spans` with buffer-space offsets
        // and their 1-based group index to `out_indices` (parallel arrays).
        // Unmatched optional groups are skipped. If PCRE2 disagrees with the
        // given span or the pattern has no groups, no entries are appended.
        void match_into(std::span<const char> buf,
                        std::size_t match_start, std::size_t match_end,
                        std::vector<Span> &out_spans,
                        std::vector<int> &out_indices) const;

    private:
        GroupMatcher() = default;
        void release() noexcept;

        pcre2_real_code_8 *code_ = nullptr;
        pcre2_real_match_data_8 *md_ = nullptr;
        int group_count_ = 0;
    };

} // namespace kestrel
