#pragma once

#include "kestrel/line_index.hpp"
#include "kestrel/scanner.hpp"
#include "kestrel/source.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace kestrel
{

    // Background worker for async search and file loading operations
    class SearchWorker
    {
    public:
        // Work submitted to background worker thread
        enum class JobType
        {
            Search,
            LoadSource
        };

        struct Job
        {
            JobType type;
            std::string pattern;                  // For search jobs
            unsigned flags;                       // For search jobs
            std::string file_path;                // For load jobs
            std::shared_ptr<const Source> source; // For search jobs - keeps mmap alive
            std::optional<LineIndex> lines;       // For search jobs - line indexing
            uint64_t generation;                  // For cancellation when newer job arrives
        };

        // Results from completed background operations
        struct Result
        {
            JobType type;
            std::vector<Match> matches;
            std::vector<std::size_t> matched_lines;
            std::string error;   // Compilation or loading error if any
            double scan_ms;      // Time taken for this operation
            uint64_t generation; // Which job produced this result

            // For LoadSource jobs
            std::shared_ptr<Source> source;
            std::optional<LineIndex> lines;
            std::string file_path;
        };

        using LoadCallback = std::function<void(std::shared_ptr<Source>, std::optional<LineIndex>, std::string, double)>;
        using SearchCallback = std::function<void(std::vector<Match>&&, std::vector<std::size_t>&&, std::string&&, double, uint64_t)>;

    public:
        SearchWorker(LoadCallback load_callback, SearchCallback search_callback);
        ~SearchWorker();

        void submit_job(Job job);
        uint64_t next_generation();

        // Test helper: check if any job is pending
        bool has_pending_job() const;

    private:
        void worker_loop();
        std::optional<Job> extract_job();
        void process_load_job(const Job& job);
        void process_search_job(const Job& job);

        LoadCallback load_callback_;
        SearchCallback search_callback_;

        std::thread worker_;
        std::atomic<bool> stop_;
        std::atomic<uint64_t> generation_;

        mutable std::mutex mutex_;
        std::condition_variable cv_;

        // Protected by mutex_
        bool job_pending_ = false;
        std::optional<Job> pending_job_;

        // Worker thread state (accessed only by worker thread)
        std::optional<Scanner> scanner_;
        std::string cached_pattern_;
        unsigned cached_flags_ = 0;
    };

} // namespace kestrel