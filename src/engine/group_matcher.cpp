#include "kestrel/group_matcher.hpp"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <hs.h>
#include <spdlog/spdlog.h>

#include <utility>

namespace kestrel
{

    namespace
    {
        uint32_t pcre2_options_from_hs_flags(unsigned hs_flags)
        {
            uint32_t opts = 0;
            if (hs_flags & HS_FLAG_CASELESS)
                opts |= PCRE2_CASELESS;
            if (hs_flags & HS_FLAG_DOTALL)
                opts |= PCRE2_DOTALL;
            if (hs_flags & HS_FLAG_MULTILINE)
                opts |= PCRE2_MULTILINE;
            return opts;
        }
    } // namespace

    std::optional<GroupMatcher> GroupMatcher::compile(std::string_view pattern, unsigned hs_flags)
    {
        int err_code = 0;
        PCRE2_SIZE err_offset = 0;

        pcre2_code *code = pcre2_compile(
            reinterpret_cast<PCRE2_SPTR>(pattern.data()),
            pattern.size(),
            pcre2_options_from_hs_flags(hs_flags),
            &err_code,
            &err_offset,
            nullptr);

        if (!code)
        {
            PCRE2_UCHAR buf[256];
            pcre2_get_error_message(err_code, buf, sizeof(buf));
            spdlog::debug("pcre2 compile failed at {}: {}", err_offset, reinterpret_cast<const char *>(buf));
            return std::nullopt;
        }

        // JIT compile skipped: the interpreter is plenty fast for per-span
        // matching against only the visible viewport, and the JIT pass adds
        // noticeable latency to pattern changes.

        uint32_t captures = 0;
        pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &captures);

        pcre2_match_data *md = pcre2_match_data_create_from_pattern(code, nullptr);
        if (!md)
        {
            pcre2_code_free(code);
            return std::nullopt;
        }

        GroupMatcher m;
        m.code_ = code;
        m.md_ = md;
        m.group_count_ = static_cast<int>(captures);
        return m;
    }

    GroupMatcher::~GroupMatcher()
    {
        release();
    }

    GroupMatcher::GroupMatcher(GroupMatcher &&other) noexcept
        : code_(other.code_), md_(other.md_), group_count_(other.group_count_)
    {
        other.code_ = nullptr;
        other.md_ = nullptr;
        other.group_count_ = 0;
    }

    GroupMatcher &GroupMatcher::operator=(GroupMatcher &&other) noexcept
    {
        if (this != &other)
        {
            release();
            code_ = other.code_;
            md_ = other.md_;
            group_count_ = other.group_count_;
            other.code_ = nullptr;
            other.md_ = nullptr;
            other.group_count_ = 0;
        }
        return *this;
    }

    void GroupMatcher::release() noexcept
    {
        if (md_)
        {
            pcre2_match_data_free(md_);
            md_ = nullptr;
        }
        if (code_)
        {
            pcre2_code_free(code_);
            code_ = nullptr;
        }
        group_count_ = 0;
    }

    void GroupMatcher::match_into(std::span<const char> buf,
                                  std::size_t match_start, std::size_t match_end,
                                  std::vector<Span> &out_spans,
                                  std::vector<int> &out_indices) const
    {
        if (group_count_ == 0 || !code_ || !md_)
            return;
        if (match_end < match_start || match_end > buf.size())
            return;

        const PCRE2_SPTR subject = reinterpret_cast<PCRE2_SPTR>(buf.data());

        // Anchor both ends so the whole span is the match; group offsets then
        // line up 1:1 with what Hyperscan already flagged.
        const uint32_t match_opts = PCRE2_ANCHORED | PCRE2_ENDANCHORED;

        // Subject length must be match_end (not buf.size()) so PCRE2_ENDANCHORED
        // locks the match end to match_end rather than the full buffer end.
        int rc = pcre2_match(
            code_,
            subject,
            match_end,         // subject length
            match_start,       // start offset
            match_opts,
            md_,
            nullptr);

        if (rc <= 0)
            return; // 0 = ovector too small (shouldn't happen); <0 = no match / error

        PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md_);
        const int n = rc; // 1 (group 0) + captured groups

        // Sanity: whole match must span [match_start, match_end). If PCRE2
        // disagrees (flag subtleties), bail rather than emit wrong offsets.
        if (ov[0] != match_start || ov[1] != match_end)
            return;

        for (int g = 1; g < n; ++g)
        {
            PCRE2_SIZE s = ov[2 * g];
            PCRE2_SIZE e = ov[2 * g + 1];
            if (s == PCRE2_UNSET || e == PCRE2_UNSET)
                continue; // optional group didn't participate
            out_spans.push_back({static_cast<std::size_t>(s), static_cast<std::size_t>(e)});
            out_indices.push_back(g);
        }
    }

} // namespace kestrel
