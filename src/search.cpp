#include "kestrel/search.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace kestrel
{

    SearchController::SearchController()
    {
        worker_ = std::make_unique<SearchWorker>(
            [this](std::shared_ptr<Source> source, std::optional<LineIndex> lines, std::string error, double load_ms) {
                on_load_complete(std::move(source), std::move(lines), std::move(error), load_ms);
            },
            [this](std::vector<Match>&& matches, std::vector<std::size_t>&& matched_lines, std::string&& error, double scan_ms, uint64_t /*generation*/) {
                on_search_complete(std::move(matches), std::move(matched_lines), std::move(error), scan_ms);
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
        pattern_.clear();
        matches_.clear();
        prefix_max_end_.clear();
        matched_lines_.clear();
        compile_error_.clear();
        dirty_ = false;
        job_pending_ = false;
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
            job_pending_ = true;
        }

        // Submit loading job to worker
        SearchWorker::Job job{
            .type = SearchWorker::JobType::LoadSource,
            .pattern = {},
            .flags = 0,
            .file_path = std::move(path_copy),
            .source = {},
            .lines = {},
            .generation = 0 // Generation not used for load jobs
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
        matches_.clear();
        prefix_max_end_.clear();
        matched_lines_.clear();
        compile_error_.clear();
        dirty_ = false;
        job_pending_ = false;
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

    // Submit new jobs after debounce timeout (first tick stamps time, second submits)
    void SearchController::tick(double now_sec)
    {
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
                prefix_max_end_.clear();
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

        uint64_t gen = worker_->next_generation();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            job_pending_ = true;
        }

        SearchWorker::Job job{
            .type = SearchWorker::JobType::Search,
            .pattern = std::move(pattern),
            .flags = flags,
            .file_path = {},
            .source = source_, // Shared ownership keeps Source alive
            .lines = lines_,   // Copy LineIndex for worker
            .generation = gen};
        worker_->submit_job(std::move(job));
    }

    void SearchController::on_load_complete(std::shared_ptr<Source> source, std::optional<LineIndex> lines, std::string error, double load_ms)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (error.empty())
        {
            source_ = std::move(source);
            lines_ = std::move(lines);

            // Clear search state
            pattern_.clear();
            matches_.clear();
            prefix_max_end_.clear();
            matched_lines_.clear();
            compile_error_.clear();
            dirty_ = false;
            last_scan_ms_ = load_ms;
        }
        else
        {
            loading_error_ = std::move(error);
        }
        loading_ = false;
        job_pending_ = false;
    }

    void SearchController::on_search_complete(std::vector<Match>&& matches, std::vector<std::size_t>&& matched_lines, std::string&& error, double scan_ms)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        matches_ = std::move(matches);
        prefix_max_end_.resize(matches_.size());
        std::size_t running_max = 0;
        for (std::size_t i = 0; i < matches_.size(); ++i)
        {
            running_max = std::max(running_max, matches_[i].end);
            prefix_max_end_[i] = running_max;
        }
        matched_lines_ = std::move(matched_lines);
        compile_error_ = std::move(error);
        last_scan_ms_ = scan_ms;
        job_pending_ = false;
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
        while (true)
        {
            tick(1.0); // Use a dummy timestamp

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!dirty_ && !job_pending_)
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
