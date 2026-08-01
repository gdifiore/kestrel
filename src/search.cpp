#include "kestrel/search.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace kestrel
{

    namespace
    {
        // Catch the common mid-typing regex errors (unmatched '(' or '[',
        // trailing '\') before paying the ~227µs hs_compile + worker round-trip.
        // Returns empty string when the pattern looks syntactically plausible;
        // otherwise a short user-visible reason.
        std::string quick_regex_validate(std::string_view pat)
        {
            int paren = 0;
            bool in_class = false;
            bool class_first = false; // ']' is literal at start of class
            for (std::size_t i = 0; i < pat.size(); ++i)
            {
                char c = pat[i];
                if (c == '\\')
                {
                    if (i + 1 == pat.size())
                        return "trailing '\\'";
                    ++i;
                    if (in_class)
                        class_first = false; // escaped char consumed first class slot
                    continue;
                }
                if (in_class)
                {
                    if (c == ']' && !class_first)
                        in_class = false;
                    class_first = false;
                }
                else if (c == '[')
                {
                    in_class = true;
                    class_first = true;
                }
                else if (c == '(')
                    ++paren;
                else if (c == ')')
                {
                    if (--paren < 0)
                        return "unmatched ')'";
                }
            }
            if (in_class)
                return "unterminated '['";
            if (paren > 0)
                return "unmatched '('";
            return {};
        }
    }

    SearchController::SearchController()
    {
        // Callbacks run on the worker thread: they only stash the result. The UI
        // thread applies it in drain_results(); see the note on that method.
        worker_ = std::make_unique<SearchWorker>(
            [this](std::shared_ptr<Source> source, std::optional<LineIndex> lines, std::string error, double load_ms, uint64_t generation)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_ = PendingResult{};
                pending_.has_value = true;
                pending_.is_load = true;
                pending_.source = std::move(source);
                pending_.lines = std::move(lines);
                pending_.error = std::move(error);
                pending_.ms = load_ms;
                pending_.generation = generation;
            },
            [this](std::vector<Match> &&matches, std::vector<std::size_t> &&matched_lines, std::string &&error, double scan_ms, uint64_t generation)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_ = PendingResult{};
                pending_.has_value = true;
                pending_.is_load = false;
                pending_.matches = std::move(matches);
                pending_.matched_lines = std::move(matched_lines);
                pending_.error = std::move(error);
                pending_.ms = scan_ms;
                pending_.generation = generation;
            });
    }

    SearchController::~SearchController()
    {
        worker_.reset();
    }

    void SearchController::load_source(std::string_view path)
    {
        source_ = std::make_shared<Source>(Source::from_path(path));
        lines_.emplace(source_->bytes());

        std::lock_guard<std::mutex> lock(mutex_);
        search_generation_ = worker_->next_generation();
        load_generation_ = search_generation_;
        pending_ = PendingResult{}; // drop any result captured for the old source
        pattern_.clear();
        matches_.clear();
        prefix_max_end_.clear();
        matched_lines_.clear();
        compile_error_.clear();
        dirty_ = false;
        job_pending_ = false;
        loading_ = false;
        loading_error_.clear();
        completed_generation_.fetch_add(1, std::memory_order_release);
    }

    void SearchController::load_source_async(std::string_view path)
    {
        std::string path_copy(path); // Copy for thread safety
        uint64_t generation = worker_->next_generation();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_ = PendingResult{}; // drop any result from a prior source/scan
            loading_ = true;
            loading_error_.clear();
            search_generation_ = generation; // invalidate in-flight scans for the old source
            load_generation_ = generation;
            job_pending_ = true;
        }

        SearchWorker::Job job{
            .type = SearchWorker::JobType::LoadSource,
            .pattern = {},
            .flags = 0,
            .file_path = std::move(path_copy),
            .source = {},
            .lines = {},
            .generation = generation,
        };
        worker_->submit_job(std::move(job));
    }

    bool SearchController::is_loading() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return loading_;
    }

    std::string SearchController::get_loading_error() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return loading_error_;
    }

    void SearchController::clear_source()
    {
        source_.reset();
        lines_.reset();

        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = PendingResult{}; // drop any result captured for the old source
        matches_.clear();
        prefix_max_end_.clear();
        matched_lines_.clear();
        compile_error_.clear();
        search_generation_ = worker_->next_generation();
        load_generation_ = search_generation_;
        dirty_ = false;
        job_pending_ = false;
        loading_ = false;
        loading_error_.clear();
        completed_generation_.fetch_add(1, std::memory_order_release);
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
        last_edit_sec_ = 0.0; // tick() will stamp on first call

        std::lock_guard<std::mutex> lock(mutex_);
        search_generation_ = worker_->next_generation();
        dirty_ = true;
    }

    // Submit new jobs after debounce timeout (first tick stamps time, second submits)
    void SearchController::tick(double now_sec)
    {
        // Apply any worker result first, on this (UI) thread.
        drain_results();

        // Handle debounced pattern updates
        bool should_submit = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!dirty_)
            {
                return; // No pending edit to process
            }

            if (last_edit_sec_ == 0.0)
            {
                last_edit_sec_ = now_sec;
                return; // Stamp time, wait for debounce
            }

            if ((now_sec - last_edit_sec_) * 1000.0 >= debounce_ms_)
            {
                should_submit = true;
                // Don't reset last_edit_sec_ here - will be reset by dirty_ = false
            }
        }

        // Submit new job if debounce elapsed and we have source
        if (should_submit && !source_)
        {
            // A query can be entered before a file is loaded. There is no job
            // to submit in that state, so it must not remain "dirty" forever.
            std::lock_guard<std::mutex> lock(mutex_);
            dirty_ = false;
            last_edit_sec_ = 0.0;
        }
        else if (should_submit)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (job_pending_)
                {
                    return; // Job already pending, don't submit another
                }
                dirty_ = false; // Clear dirty when submitting
            }

            // Reset edit state - job submitted or processed.
            last_edit_sec_ = 0.0;

            if (pattern_.empty())
            {
                // Empty pattern: clear matches immediately (no lock needed - UI thread only)
                matches_.clear();
                prefix_max_end_.clear();
                matched_lines_.clear();
                compile_error_.clear();
                last_scan_ms_ = 0.0;
                completed_generation_.fetch_add(1, std::memory_order_release);
            }
            else if (std::string reason = quick_regex_validate(pattern_); !reason.empty())
            {
                matches_.clear();
                prefix_max_end_.clear();
                matched_lines_.clear();
                compile_error_ = "regex syntax: " + reason;
                last_scan_ms_ = 0.0;
                completed_generation_.fetch_add(1, std::memory_order_release);
            }
            else
            {
                uint64_t generation = 0;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    generation = search_generation_;
                }
                submit_job(pattern_, flags_, generation);
            }
        }
    }

    // Submit scan job to worker thread with unique generation ID.
    // Worker can check generation against atomic counter to abort stale scans.
    void SearchController::submit_job(std::string pattern, unsigned flags, uint64_t generation)
    {
        if (pattern.empty() || !source_)
            return;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation != search_generation_)
                return;
            job_pending_ = true;
        }

        SearchWorker::Job job{
            .type = SearchWorker::JobType::Search,
            .pattern = std::move(pattern),
            .flags = flags,
            .file_path = {},
            .source = source_, // shared ownership keeps Source alive
            .lines = lines_,
            .generation = generation,
        };
        worker_->submit_job(std::move(job));
    }

    // UI thread. Move the worker's stashed result out from under the lock, then
    // apply it to live fields with no lock held (the UI thread is the sole writer
    // of those fields). Clearing job_pending_ here — not on the worker — ties
    // "job done" to "result applied", which wait_for_completion relies on.
    void SearchController::drain_results()
    {
        PendingResult r;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!pending_.has_value)
                return;
            r = std::move(pending_);
            pending_ = PendingResult{};
            job_pending_ = false;
            if (r.is_load && r.generation == load_generation_)
                loading_ = false;
        }

        if (r.is_load)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (r.generation != load_generation_)
                    return;
            }
            if (r.error.empty())
            {
                source_ = std::move(r.source);
                lines_ = std::move(r.lines);
                ts_index_ = TimestampIndex(source_->bytes(), *lines_);

                // Clear search state
                pattern_.clear();
                matches_.clear();
                prefix_max_end_.clear();
                matched_lines_.clear();
                compile_error_.clear();
                last_scan_ms_ = r.ms;

                std::lock_guard<std::mutex> lock(mutex_);
                dirty_ = false;
            }
            else
            {
                std::lock_guard<std::mutex> lock(mutex_);
                loading_error_ = std::move(r.error);
            }
        }
        else
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (r.generation != search_generation_)
                    return;
            }
            matches_ = std::move(r.matches);
            prefix_max_end_.resize(matches_.size());
            std::size_t running_max = 0;
            for (std::size_t i = 0; i < matches_.size(); ++i)
            {
                running_max = std::max(running_max, matches_[i].end);
                prefix_max_end_[i] = running_max;
            }
            matched_lines_ = std::move(r.matched_lines);
            compile_error_ = std::move(r.error);
            last_scan_ms_ = r.ms;
        }

        completed_generation_.fetch_add(1, std::memory_order_release);
    }

    // Relies on matches_ sorted by start (see rescan). Returns a contiguous
    // span covering every match that overlaps [lo, hi): any match whose
    // [start, end) intersects the range. The span may include extra matches
    // that do not overlap (callers must skip them) because overlap cannot be
    // expressed as a single sorted range — a predecessor with end > lo may
    // sit behind non-overlapping matches with smaller end values.
    // Uses prefix_max_end_ (running max of .end) for log-n lookup of the
    // earliest index that could overlap lo.
    std::span<const Match> SearchController::matches_in_range(size_t lo, size_t hi) const
    {
        auto pm_begin = std::upper_bound(prefix_max_end_.begin(), prefix_max_end_.end(), lo);
        std::size_t begin_idx = static_cast<std::size_t>(pm_begin - prefix_max_end_.begin());
        if (begin_idx >= matches_.size())
            return {};
        auto end = std::lower_bound(matches_.begin() + begin_idx, matches_.end(), hi,
                                    [](const Match &m, size_t o)
                                    { return m.start < o; });
        std::size_t count = static_cast<std::size_t>(end - (matches_.begin() + begin_idx));
        return std::span<const Match>(matches_.data() + begin_idx, count);
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

    void SearchController::wait_for_completion()
    {
        // Keep ticking until no job is pending
        double now = 1.0;
        while (true)
        {
            tick(now);
            now += static_cast<double>(std::max(1, debounce_ms_)) / 1000.0;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!dirty_ && !job_pending_ && !loading_)
                {
                    if (!worker_->has_pending_job())
                        break;
                }
            }

            // Brief yield to let worker thread complete
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

} // namespace kestrel
