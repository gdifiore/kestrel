#include "kestrel/search.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <array>
#include <cerrno>
#include <filesystem>
#include <thread>
#include <type_traits>

#if defined(__linux__)
#include <sys/inotify.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#endif

#include <fcntl.h>
#include <unistd.h>

namespace kestrel
{

    SearchController::SearchController()
    {
        // Callbacks run on the worker thread: they only stash the result. The UI
        // thread applies it in drain_results(); see the note on that method.
        worker_ = std::make_unique<SearchWorker>(
            [this](SearchWorker::LoadResult result)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_ = std::move(result);
            },
            [this](SearchWorker::SearchResult result)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_ = std::move(result);
            });
    }

    SearchController::~SearchController()
    {
        close_tail_watch();
        worker_.reset();
    }

    void SearchController::set_tail_mode(bool on)
    {
        if (tail_mode_ == on)
            return;
        tail_mode_ = on;
        tail_needs_refresh_ = false;
        if (tail_mode_)
            reset_tail_watch();
        else
        {
            cancel_tail_refresh();
            close_tail_watch();
        }
    }

    void SearchController::set_tail_paused(bool paused)
    {
        if (tail_paused_ == paused)
            return;
        tail_paused_ = paused;
        if (paused)
            cancel_tail_refresh();
    }

    void SearchController::cancel_tail_refresh()
    {
        if (!refresh_pending_)
            return;
        search_generation_ = worker_->next_generation();
        refresh_pending_ = false;
        job_pending_ = false;
        tail_needs_refresh_ = tail_mode_;
        dirty_ = !pattern_.empty();
    }

    void SearchController::close_tail_watch()
    {
#if defined(__linux__)
        if (inotify_fd_ != -1)
        {
            close(inotify_fd_);
            inotify_fd_ = -1;
            inotify_watch_ = -1;
        }
#elif defined(__APPLE__)
        if (tail_file_fd_ != -1)
        {
            close(tail_file_fd_);
            tail_file_fd_ = -1;
        }
        if (tail_directory_fd_ != -1)
        {
            close(tail_directory_fd_);
            tail_directory_fd_ = -1;
        }
        if (kqueue_fd_ != -1)
        {
            close(kqueue_fd_);
            kqueue_fd_ = -1;
        }
#endif
    }

    void SearchController::reset_tail_watch()
    {
        close_tail_watch();
        if (!tail_mode_ || !source_ || source_->path().empty())
            return;

#if defined(__linux__)
        inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (inotify_fd_ == -1)
        {
            spdlog::warn("inotify_init1 for follow mode failed: {}", std::strerror(errno));
            return;
        }
        const std::filesystem::path file(source_->path());
        const std::string parent = file.parent_path().empty() ? "." : file.parent_path().string();
        constexpr uint32_t mask = IN_CLOSE_WRITE | IN_MODIFY | IN_ATTRIB |
                                  IN_CREATE | IN_MOVED_TO | IN_DELETE | IN_MOVE_SELF;
        inotify_watch_ = inotify_add_watch(inotify_fd_, parent.c_str(), mask);
        if (inotify_watch_ == -1)
        {
            spdlog::warn("inotify_add_watch for '{}' failed: {}", parent, std::strerror(errno));
            close(inotify_fd_);
            inotify_fd_ = -1;
        }
#elif defined(__APPLE__)
        tail_file_fd_ = open(source_->path().c_str(), O_EVTONLY);
        if (tail_file_fd_ == -1)
        {
            spdlog::warn("open follow-mode file '{}' failed: {}", source_->path(),
                         std::strerror(errno));
            return;
        }

        const std::filesystem::path file(source_->path());
        const std::string parent = file.parent_path().empty() ? "." : file.parent_path().string();
        tail_directory_fd_ = open(parent.c_str(), O_EVTONLY);
        if (tail_directory_fd_ == -1)
        {
            spdlog::warn("open follow-mode directory '{}' failed: {}", parent,
                         std::strerror(errno));
            close_tail_watch();
            return;
        }

        kqueue_fd_ = kqueue();
        if (kqueue_fd_ == -1)
        {
            spdlog::warn("kqueue for follow mode failed: {}", std::strerror(errno));
            close_tail_watch();
            return;
        }

        std::array<struct kevent, 2> changes{};
        EV_SET(&changes[0], tail_file_fd_, EVFILT_VNODE, EV_ADD | EV_CLEAR,
               NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_RENAME | NOTE_DELETE,
               0, nullptr);
        // Directory events let follow mode retry after a delete-then-create
        // rotation, when the original file vnode no longer reports activity.
        EV_SET(&changes[1], tail_directory_fd_, EVFILT_VNODE, EV_ADD | EV_CLEAR,
               NOTE_WRITE | NOTE_RENAME | NOTE_DELETE, 0, nullptr);
        if (kevent(kqueue_fd_, changes.data(), static_cast<int>(changes.size()),
                   nullptr, 0, nullptr) == -1)
        {
            spdlog::warn("kqueue follow-mode registration failed: {}", std::strerror(errno));
            close_tail_watch();
        }
#else
        spdlog::warn("follow mode is unavailable on this platform");
#endif
    }

    void SearchController::consume_tail_events(double /*now_sec*/)
    {
        if (!tail_mode_ || !source_)
            return;

#if defined(__linux__)
        if (inotify_fd_ == -1)
            return;
        const std::filesystem::path file(source_->path());
        const std::string name = file.filename().string();
        std::array<char, 4096> bytes{};
        bool changed = false;
        while (true)
        {
            const ssize_t count = read(inotify_fd_, bytes.data(), bytes.size());
            if (count <= 0)
            {
                if (count == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
                    spdlog::warn("inotify read failed: {}", std::strerror(errno));
                break;
            }
            for (char *p = bytes.data(), *end = bytes.data() + count; p < end;)
            {
                const auto *event = reinterpret_cast<const inotify_event *>(p);
                if ((event->mask & IN_Q_OVERFLOW) ||
                    (event->len > 0 && name == event->name))
                    changed = true;
                p += sizeof(inotify_event) + event->len;
            }
        }
#elif defined(__APPLE__)
        if (kqueue_fd_ == -1)
            return;
        std::array<struct kevent, 8> events{};
        const timespec timeout{};
        const int count = kevent(kqueue_fd_, nullptr, 0, events.data(),
                                 static_cast<int>(events.size()), &timeout);
        if (count == -1)
        {
            spdlog::warn("kqueue follow-mode read failed: {}", std::strerror(errno));
            return;
        }
        bool changed = false;
        constexpr unsigned int change_mask = NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB |
                                             NOTE_RENAME | NOTE_DELETE;
        for (int i = 0; i < count; ++i)
        {
            const auto &event = events[static_cast<std::size_t>(i)];
            if ((event.fflags & change_mask) != 0 ||
                event.ident == static_cast<uintptr_t>(tail_directory_fd_))
                changed = true;
        }
#else
        return;
#endif

        tail_needs_refresh_ = tail_needs_refresh_ || changed;
    }

    void SearchController::schedule_tail_refresh()
    {
        // One active job and one accumulated watcher flag bound refresh work.
        // A replacement load always takes precedence over following the old file.
        if (!tail_mode_ || tail_paused_ || !tail_needs_refresh_ || !source_ ||
            loading_ || job_pending_ || refresh_pending_ || refresh_search_due_)
            return;
        refresh_generation_ = worker_->next_generation();
        search_generation_ = refresh_generation_;
        refresh_pending_ = true;
        job_pending_ = true;
        tail_needs_refresh_ = false;
        SearchWorker::Job job;
        job.type = SearchWorker::JobType::LoadSource;
        job.file_path = source_->path();
        job.generation = refresh_generation_;
        job.refresh = true;
        worker_->submit_job(std::move(job));
    }

    void SearchController::clear_search_results()
    {
        matches_.clear();
        prefix_max_end_.clear();
        matched_lines_.clear();
        results_truncated_ = false;
        compile_error_.clear();
        last_scan_ms_ = 0.0;
    }

    void SearchController::load_source(std::string_view path)
    {
        source_ = std::make_shared<Source>(Source::from_path(path));
        ++source_revision_;
        lines_ = std::make_shared<LineIndex>(source_->bytes());
        ts_index_ = TimestampIndex(source_->bytes(), *lines_);

        std::lock_guard<std::mutex> lock(mutex_);
        search_generation_ = worker_->next_generation();
        load_generation_ = search_generation_;
        refresh_pending_ = false;
        refresh_search_due_ = false;
        pending_.reset(); // drop any result captured for the old source
        pattern_.clear();
        clear_search_results();
        dirty_ = false;
        job_pending_ = false;
        loading_ = false;
        loading_error_.clear();
        completed_generation_.fetch_add(1, std::memory_order_release);
        reset_tail_watch();
    }

    void SearchController::load_source_async(std::string_view path)
    {
        std::string path_copy(path); // Copy for thread safety
        uint64_t generation = worker_->next_generation();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.reset(); // drop any result from a prior source/scan
            loading_ = true;
            loading_error_.clear();
            load_progress_ = std::make_shared<SearchWorker::LoadProgress>();
            search_generation_ = generation; // invalidate in-flight scans for the old source
            load_generation_ = generation;
            refresh_pending_ = false;
            refresh_search_due_ = false;
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
            .match_limit = match_limit_,
            .load_progress = load_progress_,
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

    SearchController::LoadProgressSnapshot SearchController::load_progress() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!load_progress_)
            return {};
        return {load_progress_->phase.load(std::memory_order_relaxed),
                load_progress_->completed.load(std::memory_order_relaxed),
                load_progress_->total.load(std::memory_order_relaxed)};
    }

    void SearchController::cancel_loading()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!loading_)
            return;
        search_generation_ = worker_->next_generation();
        load_generation_ = search_generation_;
        refresh_pending_ = false;
        loading_ = false;
        job_pending_ = false;
        pending_.reset();
        if (load_progress_)
            load_progress_->phase.store(SearchWorker::LoadPhase::Cancelled, std::memory_order_relaxed);
    }

    void SearchController::clear_source()
    {
        source_.reset();
        ++source_revision_;
        lines_.reset();
        ts_index_ = TimestampIndex{};

        std::lock_guard<std::mutex> lock(mutex_);
        pending_.reset(); // drop any result captured for the old source
        clear_search_results();
        search_generation_ = worker_->next_generation();
        load_generation_ = search_generation_;
        refresh_pending_ = false;
        refresh_search_due_ = false;
        dirty_ = false;
        job_pending_ = false;
        loading_ = false;
        loading_error_.clear();
        completed_generation_.fetch_add(1, std::memory_order_release);
        reset_tail_watch();
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
        search_generation_ = worker_->next_search_generation();
        dirty_ = true;
    }

    // Submit new jobs after debounce timeout (first tick stamps time, second submits)
    void SearchController::tick(double now_sec)
    {
        tail_updated_ = false;
        consume_tail_events(now_sec);
        // Apply any worker result first, on this (UI) thread.
        drain_results();
        schedule_tail_refresh();

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
                clear_search_results();
                refresh_search_due_ = false;
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
            submitted_search_generation_ = generation;
        }

        SearchWorker::Job job{
            .type = SearchWorker::JobType::Search,
            .pattern = std::move(pattern),
            .flags = flags,
            .file_path = {},
            .source = source_, // shared ownership keeps Source alive
            .lines = lines_,
            .generation = generation,
            .match_limit = match_limit_,
            .load_progress = {},
        };
        worker_->submit_job(std::move(job));
    }

    // UI thread. Move the worker's stashed result out from under the lock, then
    // apply it to live fields with no lock held (the UI thread is the sole writer
    // of those fields). Clearing job_pending_ here — not on the worker — ties
    // "job done" to "result applied", which wait_for_completion relies on.
    void SearchController::drain_results()
    {
        std::optional<PendingResult> result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result.swap(pending_);
        }
        if (!result)
            return;
        std::visit([this](auto &&value)
        {
            using Result = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Result, SearchWorker::LoadResult>)
                apply_load_result(std::move(value));
            else
                apply_search_result(std::move(value));
        }, std::move(*result));
    }

    void SearchController::apply_load_result(SearchWorker::LoadResult result)
    {
        if (result.generation != (result.refresh ? refresh_generation_ : load_generation_))
            return;
        if (result.refresh && !refresh_pending_)
            return;
        job_pending_ = false;
        if (result.refresh)
        {
            refresh_pending_ = false;
            if (!tail_mode_ || tail_paused_ || result.cancelled)
            {
                tail_needs_refresh_ = tail_mode_;
                return;
            }
            if (!result.error.empty())
            {
                spdlog::warn("follow reload failed: {}", result.error);
                return; // Keep the visible snapshot; the next event retries.
            }
        }
        else
        {
            loading_ = false;
            if (result.cancelled)
                return;
            if (!result.error.empty())
            {
                loading_error_ = std::move(result.error);
                return;
            }
            ++source_revision_;
        }
        source_ = std::move(result.source);
        lines_ = std::move(result.lines);
        ts_index_ = std::move(result.timestamps);
        clear_search_results();
        last_scan_ms_ = result.ms;
        dirty_ = !pattern_.empty();
        refresh_search_due_ = result.refresh && dirty_;
        last_edit_sec_ = 0.0;
        if (result.refresh)
        {
            tail_updated_ = true;
#if defined(__APPLE__)
            reset_tail_watch();
#endif
        }
        else
        {
            tail_needs_refresh_ = false;
            reset_tail_watch();
        }
        completed_generation_.fetch_add(1, std::memory_order_release);
    }

    void SearchController::apply_search_result(SearchWorker::SearchResult result)
    {
        // An obsolete completion must not mark a newer load/refresh as idle.
        if (loading_ || refresh_pending_ || result.generation != submitted_search_generation_)
            return;
        job_pending_ = false;
        if (result.generation != search_generation_)
            return;
        refresh_search_due_ = false;
        matches_ = std::move(result.matches);
        prefix_max_end_ = std::move(result.prefix_max_end);
        matched_lines_ = std::move(result.matched_lines);
        results_truncated_ = result.truncated;
        compile_error_ = std::move(result.error);
        last_scan_ms_ = result.ms;
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
