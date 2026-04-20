#include "kestrel/search.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace kestrel
{

    SearchController::SearchController()
        : stop_(false), generation_(1)
    {
        worker_ = std::thread(&SearchController::worker_loop, this);
    }

    SearchController::~SearchController()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_.store(true);
        }
        cv_.notify_one();
        if (worker_.joinable())
            worker_.join();
    }

    void SearchController::load_source(std::string_view path)
    {
        source_ = std::make_shared<Source>(Source::from_path(path));
        lines_.emplace(source_->bytes());

        std::lock_guard<std::mutex> lock(mutex_);
        pattern_.clear();
        matches_.clear();
        matched_lines_.clear();
        compile_error_.clear();
        dirty_ = false;
        job_pending_ = false;
        pending_job_.reset();
        latest_result_.reset();
        loading_ = false;
        loading_error_.clear();
    }

    void SearchController::load_source_async(std::string_view path)
    {
        std::string path_copy(path); // Copy for thread safety

        {
            std::lock_guard<std::mutex> lock(mutex_);
            loading_ = true;
            loading_error_.clear();
        }

        // Submit loading job to worker thread (reuse existing worker)
        auto load_job = [this, path_copy = std::move(path_copy)]() {
            try {
                auto new_source = std::make_shared<Source>(Source::from_path(path_copy));
                auto new_lines = LineIndex(new_source->bytes());

                // Atomically update state
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    source_ = new_source;
                    lines_.emplace(std::move(new_lines));

                    // Clear search state
                    pattern_.clear();
                    matches_.clear();
                    matched_lines_.clear();
                    compile_error_.clear();
                    dirty_ = false;
                    job_pending_ = false;
                    pending_job_.reset();
                    latest_result_.reset();

                    loading_ = false;
                    loading_error_.clear();
                }
                spdlog::info("loaded {} ({} bytes)", path_copy, new_source->bytes().size());
            }
            catch (const SourceError& e) {
                std::lock_guard<std::mutex> lock(mutex_);
                loading_ = false;
                loading_error_ = e.what();
                spdlog::error("async load_source failed: {} ({})", path_copy, e.what());
            }
        };

        // Run on separate thread to avoid blocking worker
        std::thread(load_job).detach();
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
        matches_.clear();
        matched_lines_.clear();
        compile_error_.clear();
        dirty_ = false;
        job_pending_ = false;
        pending_job_.reset();
        latest_result_.reset();
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
        dirty_ = true;
    }

    // Two-phase async operation:
    // 1. Process any completed scan results from worker thread
    // 2. Submit new jobs after debounce timeout (first tick stamps time, second submits)
    // RAII scopes limit mutex lifetime to avoid blocking worker thread.
    void SearchController::tick(double now_sec)
    {
        // Check for completed results first
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (latest_result_)
            {
                matches_ = std::move(latest_result_->matches);
                matched_lines_ = std::move(latest_result_->matched_lines);
                compile_error_ = std::move(latest_result_->error);
                last_scan_ms_ = latest_result_->scan_ms;
                latest_result_.reset();
                job_pending_ = false;
            }
        }

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
        if (should_submit && source_)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (job_pending_)
                {
                    return; // Job already pending, don't submit another
                }
                dirty_ = false; // Clear dirty when submitting
            }

            // Reset edit state - job submitted or processed
            last_edit_sec_ = 0.0;

            if (pattern_.empty())
            {
                // Empty pattern: clear matches immediately (no lock needed - UI thread only)
                matches_.clear();
                matched_lines_.clear();
                compile_error_.clear();
                last_scan_ms_ = 0.0;
            }
            else
            {
                submit_job(pattern_, flags_);
            }
        }
    }

    // Submit scan job to worker thread with unique generation ID.
    // Worker can check generation against atomic counter to abort stale scans.
    void SearchController::submit_job(std::string pattern, unsigned flags)
    {
        if (pattern.empty() || !source_)
            return;

        uint64_t gen = generation_.fetch_add(1) + 1;

        std::lock_guard<std::mutex> lock(mutex_);
        pending_job_ = Job{
            .pattern = std::move(pattern),
            .flags = flags,
            .source = source_, // Shared ownership keeps Source alive
            .generation = gen};
        job_pending_ = true;
        cv_.notify_one();
    }

    // Background worker thread: compiles patterns and runs scans.
    // Owns Scanner instance to avoid cross-thread vectorscan issues.
    // Supports cancellation via generation counter polling.
    void SearchController::worker_loop()
    {
        std::optional<Scanner> scanner;
        std::string cached_pattern;
        unsigned cached_flags = 0;

        while (true)
        {
            std::optional<Job> job;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]
                         { return stop_.load() || pending_job_; });

                if (stop_.load())
                    break;

                job = std::move(pending_job_);
                pending_job_.reset();
            }

            if (!job)
                continue;

            auto t0 = std::chrono::steady_clock::now();
            Result result{
                .matches = {},
                .matched_lines = {},
                .error = {},
                .scan_ms = 0.0,
                .generation = job->generation};

            try
            {
                // Reuse scanner if pattern and flags match (avoids expensive recompilation)
                if (!scanner || job->pattern != cached_pattern || job->flags != cached_flags) {
                    scanner.emplace(job->pattern, job->flags);
                    cached_pattern = job->pattern;
                    cached_flags = job->flags;
                }
                auto span = job->source->bytes();
                spdlog::debug("worker scanning pattern '{}' on {} bytes", job->pattern, span.size());
                result.matches = scanner->scan(
                    std::string_view{span.data(), span.size()}, // Convert span to string_view
                    &generation_, job->generation);
                spdlog::debug("worker found {} matches", result.matches.size());

                // Build matched_lines with deduplication (requires lines_)
                if (lines_ && !result.matches.empty())
                {
                    // Estimate: assume average 100 chars per line for better allocation
                    std::size_t estimated_lines = result.matches.size() / 100 + 1;
                    result.matched_lines.reserve(std::min(estimated_lines, result.matches.size()));

                    // Incremental line tracking (matches are in offset order)
                    std::size_t current_line = lines_->line_of(result.matches[0].start);
                    result.matched_lines.push_back(current_line);

                    for (std::size_t i = 1; i < result.matches.size(); ++i)
                    {
                        // Only do binary search when we might have crossed a line boundary
                        if (result.matches[i].start >= lines_->line_start(current_line + 1))
                        {
                            current_line = lines_->line_of(result.matches[i].start);
                            if (current_line != result.matched_lines.back())
                            {
                                result.matched_lines.push_back(current_line);
                            }
                        }
                    }
                }
            }
            catch (const ScannerError &e)
            {
                result.error = e.what();
                spdlog::error("worker scan failed: {}", result.error);
                scanner.reset(); // Clear failed scanner
                cached_pattern.clear(); // Clear cache
            }

            auto t1 = std::chrono::steady_clock::now();
            result.scan_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            spdlog::debug("rescan pattern='{}' flags={:#x} matches={} time={:.2f}ms",
                          job->pattern, job->flags, result.matches.size(), result.scan_ms);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                latest_result_ = std::move(result);
            }
        }
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

    void SearchController::wait_for_completion()
    {
        // Keep ticking until no job is pending
        while (true)
        {
            tick(1.0); // Use a dummy timestamp

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!dirty_ && !job_pending_ && !latest_result_)
                {
                    break;
                }
            }

            // Brief yield to let worker thread complete
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

} // namespace kestrel
