#include "kestrel/search.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace kestrel
{

    SearchController::SearchController() = default;

    void SearchController::load_source(std::string_view path)
    {
        source_.emplace(Source::from_path(path));
        lines_.emplace(source_->bytes());
        dirty_ = true;
        pattern_.clear();
        matches_.clear();
    }

    void SearchController::clear_source()
    {
        source_.reset();
        lines_.reset();
        matches_.clear();
    }

    std::span<const char> SearchController::source_bytes() const
    {
        return source_ ? source_->bytes() : std::span<const char>{};
    }

    const LineIndex &SearchController::line_index() const
    {
        return *lines_;
    }

    void SearchController::set_pattern(std::string_view p, unsigned flags)
    {
        if (p == pattern_ && flags == flags_)
            return;
        pattern_.assign(p);
        flags_ = flags;
        dirty_ = true;
        last_edit_sec_ = 0.0; // tick() will stamp on first call
    }

    // Two-phase debounce: first tick after set_pattern() stamps the edit time,
    // subsequent ticks rescan once debounce_ms_ has elapsed. Keeps typing cheap.
    void SearchController::tick(double now_sec)
    {
        if (!dirty_)
            return;
        if (last_edit_sec_ == 0.0)
        {
            last_edit_sec_ = now_sec;
            return;
        }
        if ((now_sec - last_edit_sec_) * 1000.0 < debounce_ms_)
            return;
        rescan();
        dirty_ = false;
    }

    void SearchController::rescan()
    {
        matches_.clear();
        matched_lines_.clear();
        compile_error_.clear();
        scanner_.reset();
        last_scan_ms_ = 0.0;
        if (pattern_.empty() || !source_)
            return;

        auto t0 = std::chrono::steady_clock::now();
        try
        {
            scanner_.emplace(pattern_, flags_);
            auto bytes = source_->bytes();
            matches_ = scanner_->scan(std::string_view{bytes.data(), bytes.size()});
            // Hyperscan does not guarantee callback order; sort so downstream
            // (matched_lines dedup, binary-search lookups) can assume ascending start.
            std::sort(matches_.begin(), matches_.end());
            if (lines_)
            {
                matched_lines_.reserve(matches_.size());
                std::size_t last = static_cast<std::size_t>(-1);
                for (const auto &m : matches_)
                {
                    std::size_t line = lines_->line_of(m.start);
                    if (line != last)
                    {
                        matched_lines_.push_back(line);
                        last = line;
                    }
                }
            }
        }
        catch (const ScannerError &e)
        {
            compile_error_ = e.what();
            spdlog::debug("pattern compile failed: {}", e.what());
        }
        auto t1 = std::chrono::steady_clock::now();
        last_scan_ms_ = std::chrono::duration<double, std::milli>(t1 - t0).count();
        spdlog::debug("rescan pattern='{}' flags={:#x} matches={} time={:.2f}ms",
                      pattern_, flags_, matches_.size(), last_scan_ms_);
    }

    // Relies on matches_ sorted by start (see rescan). Returns matches whose
    // start falls in [lo, hi); matches extending past hi are included.
    std::span<const Match> SearchController::matches_in_range(size_t lo, size_t hi) const
    {
        auto begin = std::lower_bound(matches_.begin(), matches_.end(), lo,
                                      [](const Match &m, size_t o)
                                      { return m.start < o; });
        auto end = std::lower_bound(begin, matches_.end(), hi,
                                    [](const Match &m, size_t o)
                                    { return m.start < o; });
        return std::span<const Match>(&*begin, end - begin);
    }

    std::size_t SearchController::matches_before(std::size_t offset) const
    {
        auto it = std::lower_bound(matches_.begin(), matches_.end(), offset,
                                   [](const Match &m, std::size_t o)
                                   { return m.start < o; });
        return static_cast<std::size_t>(it - matches_.begin());
    }

    std::size_t SearchController::matches_after(std::size_t offset) const
    {
        auto it = std::upper_bound(matches_.begin(), matches_.end(), offset,
                                   [](std::size_t o, const Match &m)
                                   { return o < m.start; });
        return static_cast<std::size_t>(matches_.end() - it);
    }

} // namespace kestrel
