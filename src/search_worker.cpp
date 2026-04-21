#include "kestrel/search_worker.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace kestrel
{

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

    void SearchWorker::process_load_job(const Job& job)
    {
        auto t0 = std::chrono::steady_clock::now();

        std::shared_ptr<Source> source;
        std::optional<LineIndex> lines;
        std::string error;

        try
        {
            source = std::make_shared<Source>(Source::from_path(job.file_path));
            lines.emplace(source->bytes());  // Direct construction, no extra moves

            spdlog::debug("loaded {} ({} bytes)", job.file_path, source->bytes().size());
        }
        catch (const SourceError &e)
        {
            error = e.what();
            spdlog::error("async load_source failed: {} ({})", job.file_path, e.what());
        }

        auto t1 = std::chrono::steady_clock::now();
        double load_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        load_callback_(std::move(source), std::move(lines), std::move(error), load_ms);
    }

    void SearchWorker::process_search_job(const Job& job)
    {
        auto t0 = std::chrono::steady_clock::now();

        std::vector<Match> matches;
        std::vector<std::size_t> matched_lines;
        std::string error;

        try
        {
            // Reuse scanner if pattern and flags match (avoids expensive recompilation)
            if (!scanner_ || job.pattern != cached_pattern_ || job.flags != cached_flags_)
            {
                scanner_.emplace(job.pattern, job.flags);
                cached_pattern_ = job.pattern;
                cached_flags_ = job.flags;
            }
            auto span = job.source->bytes();
            spdlog::debug("worker scanning pattern '{}' on {} bytes", job.pattern, span.size());
            matches = scanner_->scan(
                std::string_view{span.data(), span.size()}, // Convert span to string_view
                &generation_, job.generation);
            spdlog::debug("worker found {} matches", matches.size());

            // Build matched_lines with deduplication (requires lines_)
            if (job.lines && !matches.empty())
            {
                const auto& lines = *job.lines;
                // Estimate: assume average 100 chars per line for better allocation
                std::size_t estimated_lines = matches.size() / 100 + 1;
                matched_lines.reserve(std::min(estimated_lines, matches.size()));

                // Incremental line tracking (matches are in offset order)
                std::size_t current_line = lines.line_of(matches[0].start);
                matched_lines.push_back(current_line);

                for (std::size_t i = 1; i < matches.size(); ++i)
                {
                    // Only do binary search when we might have crossed a line boundary
                    if (current_line + 1 < lines.line_count() &&
                        matches[i].start >= lines.line_start(current_line + 1))
                    {
                        current_line = lines.line_of(matches[i].start);
                        if (current_line != matched_lines.back())
                        {
                            matched_lines.push_back(current_line);
                        }
                    }
                }
            }
        }
        catch (const ScannerError &e)
        {
            error = e.what();
            spdlog::error("worker scan failed: {}", error);
            scanner_.reset();        // Clear failed scanner
            cached_pattern_.clear(); // Clear cache
        }

        auto t1 = std::chrono::steady_clock::now();
        double scan_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        spdlog::debug("rescan pattern='{}' flags={:#x} matches={} time={:.2f}ms",
                      job.pattern, job.flags, matches.size(), scan_ms);

        search_callback_(std::move(matches), std::move(matched_lines), std::move(error), scan_ms, job.generation);
    }

} // namespace kestrel