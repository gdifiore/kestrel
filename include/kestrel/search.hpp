#pragma once

#include "kestrel/line_index.hpp"
#include "kestrel/scanner.hpp"
#include "kestrel/source.hpp"
#include "kestrel/search_worker.hpp"
#include "kestrel/timestamp_index.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel
{

    // Async regex search controller. Runs pattern compilation and scanning
    // on a background worker thread to keep UI responsive during heavy operations.
    // Uses generation-based cancellation to abort stale scans when pattern changes.
    class SearchController
    {

    public:
        struct LoadProgressSnapshot
        {
            SearchWorker::LoadPhase phase = SearchWorker::LoadPhase::Opening;
            std::size_t completed = 0;
            std::size_t total = 0;
        };
        SearchController();
        ~SearchController();

        void load_source(std::string_view path);       // throws SourceError
        void load_source_async(std::string_view path); // async version
        bool is_loading() const noexcept;
        std::string get_loading_error() const;
        LoadProgressSnapshot load_progress() const noexcept;
        void cancel_loading();
        void clear_source();
        bool has_source() const noexcept { return source_ != nullptr; }
        std::span<const char> source_bytes() const;
        const LineIndex &line_index() const; // precondition: has_source()
        const TimestampIndex &timestamp_index() const noexcept { return ts_index_; }

        void set_pattern(std::string_view pattern, unsigned flags);
        void set_debounce_ms(int ms) noexcept { debounce_ms_ = ms; }
        void set_match_limit(std::size_t limit) noexcept { match_limit_ = limit; }

        void set_tail_mode(bool on);
        bool tail_mode() const noexcept { return tail_mode_; }
        void set_tail_paused(bool paused) noexcept { tail_paused_ = paused; }
        bool tail_paused() const noexcept { return tail_paused_; }
        bool tail_updated_last_tick() const noexcept { return tail_updated_; }

        // Called each frame with monotonic time in seconds.
        // Processes completed scan results and submits new jobs after debounce.
        void tick(double now_sec);

        const std::vector<Match> &matches() const noexcept { return matches_; }
        const std::vector<std::size_t> &matched_lines() const noexcept { return matched_lines_; }
        bool results_truncated() const noexcept { return results_truncated_; }
        bool pattern_empty() const noexcept { return pattern_.empty(); }
        const std::string &compile_error() const noexcept { return compile_error_; }
        bool is_compiling() const noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return dirty_ || job_pending_;
        }
        double last_scan_ms() const noexcept { return last_scan_ms_; }

        std::span<const Match> matches_in_range(size_t lo, size_t hi) const;

        // cursor-relative counts; offset is a byte offset into source
        std::size_t matches_before(std::size_t offset) const;
        std::size_t matches_after(std::size_t offset) const;

        // Monotonic counter bumped each time a scan or load completes.
        // Lets cache-keys (e.g. UI view_lines) detect that matched_lines /
        // total_lines / source identity changed without comparing buffers.
        uint64_t completed_generation() const noexcept { return completed_generation_.load(std::memory_order_acquire); }

        // Test helper: wait for any pending job to complete
        void wait_for_completion();

    private:
        void submit_job(std::string pattern, unsigned flags, uint64_t generation);

        // Apply any result the worker stashed. UI-thread only: called at the top
        // of tick() so the live fields below (matches_, source_, lines_, ...) are
        // mutated by the UI thread alone and readers need no lock. The worker
        // thread only ever writes into pending_ (under mutex_), never live state.
        void drain_results();
        void close_tail_watch();
        void reset_tail_watch();
        void consume_tail_events(double now_sec);
        void reload_tail_source(double now_sec);

        std::shared_ptr<Source> source_;
        std::shared_ptr<LineIndex> lines_;
        TimestampIndex ts_index_;

        std::string pattern_;
        unsigned flags_ = 0;
        double last_edit_sec_ = 0.0;
        int debounce_ms_ = 150;
        // Prevent broad expressions from materializing an unbounded result set.
        // The scan stops at this many retained matches and reports truncation.
        std::size_t match_limit_ = 2'000'000;

        // Results from completed scans
        std::vector<Match> matches_;
        // Running maximum of matches_[0..i].end, used by matches_in_range to
        // locate matches that started before `lo` but extend into it.
        std::vector<std::size_t> prefix_max_end_;
        std::vector<std::size_t> matched_lines_;
        bool results_truncated_ = false;
        std::string compile_error_;
        double last_scan_ms_ = 0.0;

        // Background worker
        std::unique_ptr<SearchWorker> worker_;

        // Protected by mutex_
        mutable std::mutex mutex_;
        bool dirty_ = false;
        bool job_pending_ = false;

        // Async loading state (protected by mutex_)
        bool loading_ = false;
        std::string loading_error_;
        uint64_t search_generation_ = 0;
        uint64_t load_generation_ = 0;
        std::shared_ptr<SearchWorker::LoadProgress> load_progress_;

        // Worker -> UI handoff. The worker stashes one completed result here
        // (under mutex_); the UI thread moves it out in drain_results() and
        // applies it to the live fields above. Latest result wins on overwrite.
        struct PendingResult
        {
            bool has_value = false;
            bool is_load = false;
            std::shared_ptr<Source> source;         // load
            std::shared_ptr<LineIndex> lines;        // load
            TimestampIndex timestamps;               // load
            std::vector<Match> matches;             // search
            std::vector<std::size_t> prefix_max_end; // search
            std::vector<std::size_t> matched_lines; // search
            bool truncated = false;
            std::string error;
            bool cancelled = false;
            double ms = 0.0;
            uint64_t generation = 0;
        };
        PendingResult pending_; // protected by mutex_

        std::atomic<uint64_t> completed_generation_{0};

        // UI-thread-only follow-mode watcher state. Linux watches the parent
        // directory with inotify; macOS watches the opened file with kqueue.
#if defined(__linux__)
        int inotify_fd_ = -1;
        int inotify_watch_ = -1;
#elif defined(__APPLE__)
        int kqueue_fd_ = -1;
        int tail_file_fd_ = -1;
        int tail_directory_fd_ = -1;
#endif
        bool tail_mode_ = false;
        bool tail_paused_ = false;
        bool tail_needs_refresh_ = false;
        bool tail_updated_ = false;
    };

} // namespace kestrel
