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
        source_.emplace(Source::from_path(path));
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
            if (latest_result_) {
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
            if (!dirty_) {
                return; // No pending edit to process
            }

            if (last_edit_sec_ == 0.0) {
                last_edit_sec_ = now_sec;
                return; // Stamp time, wait for debounce
            }

            if ((now_sec - last_edit_sec_) * 1000.0 >= debounce_ms_) {
                should_submit = true;
                // Don't reset last_edit_sec_ here - will be reset by dirty_ = false
            }
        }

        // Submit new job if debounce elapsed and we have source
        if (should_submit && source_) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (job_pending_) {
                    return; // Job already pending, don't submit another
                }
                dirty_ = false; // Clear dirty when submitting
            }

            // Reset edit state - job submitted or processed
            last_edit_sec_ = 0.0;

            if (pattern_.empty()) {
                // Empty pattern: clear matches immediately (no lock needed - UI thread only)
                matches_.clear();
                matched_lines_.clear();
                compile_error_.clear();
                last_scan_ms_ = 0.0;
            } else {
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
            .source = source_->bytes(),
            .generation = gen
        };
        job_pending_ = true;
        cv_.notify_one();
    }

    // Background worker thread: compiles patterns and runs scans.
    // Owns Scanner instance to avoid cross-thread vectorscan issues.
    // Supports cancellation via generation counter polling.
    void SearchController::worker_loop()
    {
        std::optional<Scanner> scanner;

        while (true) {
            std::optional<Job> job;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_.load() || pending_job_; });

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
                .generation = job->generation
            };

            try {
                scanner.emplace(job->pattern, job->flags);
                result.matches = scanner->scan(
                    std::string_view{job->source.data(), job->source.size()},
                    &generation_, job->generation
                );

                // Sort matches for binary search compatibility
                std::sort(result.matches.begin(), result.matches.end());

                // Build matched_lines with deduplication (requires lines_)
                if (lines_) {
                    result.matched_lines.reserve(result.matches.size());
                    std::size_t last = static_cast<std::size_t>(-1);
                    for (const auto &m : result.matches) {
                        std::size_t line = lines_->line_of(m.start);
                        if (line != last) {
                            result.matched_lines.push_back(line);
                            last = line;
                        }
                    }
                }
            } catch (const ScannerError &e) {
                result.error = e.what();
                spdlog::debug("pattern compile failed: {}", e.what());
                scanner.reset(); // Clear failed scanner
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
        while (true) {
            tick(1.0); // Use a dummy timestamp

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!dirty_ && !job_pending_ && !latest_result_) {
                    break;
                }
            }

            // Brief yield to let worker thread complete
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

} // namespace kestrel
