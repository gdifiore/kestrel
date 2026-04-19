#pragma once

#include "kestrel/line_index.hpp"
#include "kestrel/scanner.hpp"
#include "kestrel/source.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace kestrel
{

    // Async regex search controller. Runs pattern compilation and scanning
    // on a background worker thread to keep UI responsive during heavy operations.
    // Uses generation-based cancellation to abort stale scans when pattern changes.
    class SearchController
    {
        // Work submitted to background worker thread
        struct Job {
            std::string pattern;
            unsigned flags;
            std::span<const char> source;
            uint64_t generation;  // For cancellation when newer job arrives
        };

        // Results from completed background scan
        struct Result {
            std::vector<Match> matches;
            std::vector<std::size_t> matched_lines;
            std::string error;           // Compilation error if any
            double scan_ms;              // Time taken for this scan
            uint64_t generation;         // Which job produced this result
        };

    public:
        SearchController();
        ~SearchController();

        void load_source(std::string_view path); // throws SourceError
        void clear_source();
        bool has_source() const noexcept { return source_.has_value(); }
        std::span<const char> source_bytes() const;
        const LineIndex &line_index() const; // precondition: has_source()

        void set_pattern(std::string_view pattern, unsigned flags);
        void set_debounce_ms(int ms) noexcept { debounce_ms_ = ms; }

        // Called each frame with monotonic time in seconds.
        // Processes completed scan results and submits new jobs after debounce.
        void tick(double now_sec);

        const std::vector<Match> &matches() const noexcept { return matches_; }
        const std::vector<std::size_t> &matched_lines() const noexcept { return matched_lines_; }
        bool pattern_empty() const noexcept { return pattern_.empty(); }
        const std::string &compile_error() const noexcept { return compile_error_; }
        bool is_compiling() const noexcept {
            std::lock_guard<std::mutex> lock(mutex_);
            return dirty_ || job_pending_;
        }
        double last_scan_ms() const noexcept { return last_scan_ms_; }

        std::span<const Match> matches_in_range(size_t lo, size_t hi) const;

        // cursor-relative counts; offset is a byte offset into source
        std::size_t matches_before(std::size_t offset) const;
        std::size_t matches_after(std::size_t offset) const;

        // Test helper: wait for any pending job to complete
        void wait_for_completion();

    private:
        void worker_loop();
        void submit_job(std::string pattern, unsigned flags);

        std::optional<Source> source_;
        std::optional<LineIndex> lines_;

        std::string pattern_;
        unsigned flags_ = 0;
        double last_edit_sec_ = 0.0;
        int debounce_ms_ = 150;

        // Results from completed scans
        std::vector<Match> matches_;
        std::vector<std::size_t> matched_lines_;
        std::string compile_error_;
        double last_scan_ms_ = 0.0;

        // Async worker state
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::thread worker_;
        std::atomic<bool> stop_;
        std::atomic<uint64_t> generation_;

        // Protected by mutex_
        bool dirty_ = false;
        bool job_pending_ = false;
        std::optional<Job> pending_job_;
        std::optional<Result> latest_result_;
    };

} // namespace kestrel
