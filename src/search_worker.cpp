#include "kestrel/search_worker.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace kestrel
{
    namespace
    {
        void build_search_indexes(const std::vector<Match> &matches,
                                  const LineIndex *lines,
                                  std::vector<std::size_t> &prefix_max_end,
                                  std::vector<std::size_t> &matched_lines)
        {
            prefix_max_end.resize(matches.size());
            matched_lines.clear();
            if (lines)
                matched_lines.reserve(std::min(matches.size(), lines->line_count()));

            const std::span<const std::size_t> starts = lines ? lines->line_starts() :
                                                               std::span<const std::size_t>{};
            std::size_t running_max = 0;
            std::size_t line = 0;
            for (std::size_t i = 0; i < matches.size(); ++i)
            {
                const Match &match = matches[i];
                running_max = std::max(running_max, match.end);
                prefix_max_end[i] = running_max;
                if (!lines)
                    continue;
                while (line + 1 < starts.size() && match.start >= starts[line + 1])
                    ++line;
                if (matched_lines.empty() || matched_lines.back() != line)
                    matched_lines.push_back(line);
            }
        }
    }

    SearchWorker::SearchWorker(LoadCallback load_callback, SearchCallback search_callback)
        : load_callback_(load_callback), search_callback_(search_callback), stop_(false), generation_(1)
    {
        worker_ = std::thread(&SearchWorker::worker_loop, this);
    }

    SearchWorker::~SearchWorker()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_.store(true);
            generation_.fetch_add(1, std::memory_order_relaxed);
        }
        cv_.notify_one();
        if (worker_.joinable())
            worker_.join();
    }

    void SearchWorker::submit_job(Job job)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_job_ = std::move(job);
        job_pending_ = true;
        cv_.notify_one();
    }

    uint64_t SearchWorker::next_generation()
    {
        return generation_.fetch_add(1) + 1;
    }

    bool SearchWorker::has_pending_job() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return job_pending_;
    }

    // Background worker thread: compiles patterns and runs scans.
    // Owns Scanner instance to avoid cross-thread vectorscan issues.
    // Supports cancellation via generation counter polling.
    void SearchWorker::worker_loop()
    {
        while (true)
        {
            auto job = extract_job();
            if (!job)
                break;

            if (job->type == JobType::LoadSource)
            {
                process_load_job(*job);
            }
            else if (job->type == JobType::Search)
            {
                process_search_job(*job);
            }
        }
    }

    std::optional<SearchWorker::Job> SearchWorker::extract_job()
    {
        std::optional<Job> job;

        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]
                 { return stop_.load() || job_pending_; });

        if (stop_.load())
            return std::nullopt;

        job = std::move(pending_job_);
        pending_job_.reset();
        job_pending_ = false;

        return job;
    }

    void SearchWorker::process_load_job(const Job &job)
    {
        auto t0 = std::chrono::steady_clock::now();
        LoadOutcome outcome = build_load(job);
        auto t1 = std::chrono::steady_clock::now();
        const double load_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        load_callback_(std::move(outcome.source), std::move(outcome.lines),
                       std::move(outcome.timestamps), std::move(outcome.error),
                       outcome.cancelled, load_ms, job.generation);
    }

    bool SearchWorker::is_current(const Job &job) const noexcept
    {
        return generation_.load(std::memory_order_relaxed) == job.generation;
    }

    void SearchWorker::set_load_progress(const Job &job, LoadPhase phase,
                                         std::size_t completed, std::size_t total) const
    {
        if (!job.load_progress)
            return;
        job.load_progress->phase.store(phase, std::memory_order_relaxed);
        job.load_progress->completed.store(completed, std::memory_order_relaxed);
        job.load_progress->total.store(total, std::memory_order_relaxed);
    }

    SearchWorker::LoadOutcome SearchWorker::build_load(const Job &job)
    {
        LoadOutcome outcome;
        if (!is_current(job))
        {
            outcome.cancelled = true;
            set_load_progress(job, LoadPhase::Cancelled, 0, 0);
            return outcome;
        }

        try
        {
            outcome.source = std::make_shared<Source>(Source::from_path(job.file_path));
            if (!is_current(job))
                throw LoadCancelled();

            const auto report = [&](std::size_t completed, std::size_t total)
            {
                if (job.load_progress)
                    set_load_progress(job,
                                      job.load_progress->phase.load(std::memory_order_relaxed),
                                      completed, total);
                return is_current(job);
            };
            set_load_progress(job, LoadPhase::IndexingLines, 0, outcome.source->bytes().size());
            outcome.lines = std::make_shared<LineIndex>(outcome.source->bytes(), report);
            set_load_progress(job, LoadPhase::IndexingTimestamps, 0, outcome.lines->line_count());
            outcome.timestamps = TimestampIndex(outcome.source->bytes(), *outcome.lines, report);
            set_load_progress(job, LoadPhase::Complete, 1, 1);

            spdlog::debug("loaded {} ({} bytes)", job.file_path, outcome.source->bytes().size());
        }
        catch (const LoadCancelled &)
        {
            outcome.cancelled = true;
            set_load_progress(job, LoadPhase::Cancelled, 0, 0);
        }
        catch (const SourceError &e)
        {
            outcome.error = e.what();
            set_load_progress(job, LoadPhase::Failed, 0, 0);
            spdlog::error("async load_source failed: {} ({})", job.file_path, e.what());
        }
        return outcome;
    }

    void SearchWorker::process_search_job(const Job &job)
    {
        auto t0 = std::chrono::steady_clock::now();
        SearchOutcome outcome = run_search(job);

        auto t1 = std::chrono::steady_clock::now();
        double scan_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        spdlog::debug("rescan pattern='{}' flags={:#x} matches={} time={:.2f}ms",
                      job.pattern, job.flags, outcome.matches.size(), scan_ms);

        search_callback_(std::move(outcome.matches), std::move(outcome.prefix_max_end),
                         std::move(outcome.matched_lines), outcome.truncated,
                         std::move(outcome.error), scan_ms, job.generation);
    }

    SearchWorker::SearchOutcome SearchWorker::run_search(const Job &job)
    {
        SearchOutcome outcome;
        try
        {
            Scanner &scanner = get_or_compile(job.pattern, job.flags);
            const auto span = job.source->bytes();
            spdlog::debug("worker scanning pattern '{}' on {} bytes", job.pattern, span.size());
            outcome.matches = scanner.scan(
                std::string_view{span.data(), span.size()},
                &generation_, job.generation, job.match_limit, &outcome.truncated);
            build_search_indexes(outcome.matches, job.lines.get(),
                                 outcome.prefix_max_end, outcome.matched_lines);
            spdlog::debug("worker found {} matches", outcome.matches.size());
        }
        catch (const ScannerError &e)
        {
            outcome.error = e.what();
            spdlog::error("worker scan failed: {}", outcome.error);
            discard_compiled(job);
        }
        return outcome;
    }

    void SearchWorker::discard_compiled(const Job &job)
    {
        // A compile failure creates no entry; a scan-time failure should not pin
        // the associated database and scratch block in the LRU.
        const auto it = std::find_if(compile_cache_.begin(), compile_cache_.end(),
                                     [&](const CompiledEntry &entry)
                                     {
                                         return entry.pattern == job.pattern &&
                                                entry.flags == job.flags;
                                     });
        if (it != compile_cache_.end())
            compile_cache_.erase(it);
    }

    // LRU lookup keyed on (pattern, flags). Hit: splice to front, return.
    // Miss: compile (may throw ScannerError), push front, evict tail at cap.
    Scanner &SearchWorker::get_or_compile(const std::string &pattern, unsigned flags)
    {
        auto it = std::find_if(compile_cache_.begin(), compile_cache_.end(),
                               [&](const CompiledEntry &e)
                               {
                                   return e.pattern == pattern && e.flags == flags;
                               });
        if (it != compile_cache_.end())
        {
            compile_cache_.splice(compile_cache_.begin(), compile_cache_, it);
            return compile_cache_.front().scanner;
        }

        compile_cache_.push_front(CompiledEntry{pattern, flags, Scanner(pattern, flags)});
        if (compile_cache_.size() > COMPILE_CACHE_MAX)
            compile_cache_.pop_back();
        return compile_cache_.front().scanner;
    }

} // namespace kestrel
