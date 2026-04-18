#pragma once

#include "kestrel/line_index.hpp"
#include "kestrel/scanner.hpp"
#include "kestrel/source.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel
{

    class SearchController
    {
    public:
        SearchController();

        void load_source(std::string_view path); // throws SourceError
        void clear_source();
        bool has_source() const noexcept { return source_.has_value(); }
        std::span<const char> source_bytes() const;
        const LineIndex &line_index() const; // precondition: has_source()

        void set_pattern(std::string_view pattern, unsigned flags);
        void set_debounce_ms(int ms) noexcept { debounce_ms_ = ms; }

        // Called each frame with monotonic time in seconds.
        // Compiles + scans if dirty and debounce elapsed.
        void tick(double now_sec);

        const std::vector<Match> &matches() const noexcept { return matches_; }
        const std::vector<std::size_t> &matched_lines() const noexcept { return matched_lines_; }
        bool pattern_empty() const noexcept { return pattern_.empty(); }
        const std::string &compile_error() const noexcept { return compile_error_; }
        bool is_compiling() const noexcept { return dirty_; }
        double last_scan_ms() const noexcept { return last_scan_ms_; }

        // cursor-relative counts; offset is a byte offset into source
        std::size_t matches_before(std::size_t offset) const;
        std::size_t matches_after(std::size_t offset) const;

    private:
        void rescan();

        std::optional<Source> source_;
        std::optional<LineIndex> lines_;

        std::string pattern_;
        unsigned flags_ = 0;
        bool dirty_ = false;
        double last_edit_sec_ = 0.0;
        int debounce_ms_ = 150;

        std::optional<Scanner> scanner_;
        std::vector<Match> matches_;
        std::vector<std::size_t> matched_lines_;
        std::string compile_error_;
        double last_scan_ms_ = 0.0;
    };

} // namespace kestrel
