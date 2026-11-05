#pragma once

#include "kestrel/line_index.hpp"
#include "kestrel/load_progress.hpp"
#include "kestrel/scanner.hpp"
#include "kestrel/source.hpp"
#include "kestrel/timestamp_index.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <limits>
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
        using LoadPhase = kestrel::LoadPhase;
        using LoadProgress = kestrel::LoadProgress;
        // Work submitted to background worker thread
        enum class JobType
        {
            Search,
            LoadSource
        };

        struct Job
        {
            JobType type{};
            std::string pattern;                  // For search jobs
            unsigned flags = 0;                   // For search jobs
            std::string file_path;                // For load jobs
            std::shared_ptr<const Source> source; // For search jobs - keeps mmap alive
            std::shared_ptr<const LineIndex> lines; // For search jobs - shared, avoids copying the index
            uint64_t generation = 0;              // For cancellation when newer job arrives
            std::size_t match_limit = std::numeric_limits<std::size_t>::max();
            std::shared_ptr<LoadProgress> load_progress;
            bool refresh = false;
        };

        struct LoadResult
        {
            std::shared_ptr<Source> source;
            std::shared_ptr<LineIndex> lines;
            TimestampIndex timestamps;
            std::string error;
            bool cancelled = false;
            bool refresh = false;
            double ms = 0.0;
            uint64_t generation = 0;
        };

        struct SearchResult
        {
            std::vector<Match> matches;
            std::vector<std::size_t> prefix_max_end;
            std::vector<std::size_t> matched_lines;
            std::string error;
            bool truncated = false;
            double ms = 0.0;
            uint64_t generation = 0;
        };

        using LoadCallback = std::function<void(LoadResult)>;
        using SearchCallback = std::function<void(SearchResult)>;

    public:
        SearchWorker(LoadCallback load_callback, SearchCallback search_callback);
        ~SearchWorker();

        void submit_job(Job job);
        uint64_t next_generation();
        uint64_t next_search_generation();

        // Test helper: check if any job is pending
        bool has_pending_job() const;

    private:
        void worker_loop();
        std::optional<Job> extract_job();
        void process_load_job(const Job &job);
        void process_search_job(const Job &job);
        bool is_current(const Job &job) const noexcept;
        void set_load_progress(const Job &job, LoadPhase phase,
                               std::size_t completed, std::size_t total) const;

        LoadResult build_load(const Job &job);
        SearchResult run_search(const Job &job);
        void discard_compiled(const Job &job);

        LoadCallback load_callback_;
        SearchCallback search_callback_;

        std::thread worker_;
        std::atomic<bool> stop_;
        std::atomic<uint64_t> generation_;
        std::atomic<uint64_t> load_generation_{1};

        mutable std::mutex mutex_;
        std::condition_variable cv_;

        // Protected by mutex_
        bool job_pending_ = false;
        std::optional<Job> pending_job_;

        // Worker thread state (accessed only by worker thread).
        // LRU of compiled patterns: front = most recently used. Bounded so
        // stale patterns don't pin Hyperscan database memory indefinitely.
        struct CompiledEntry
        {
            std::string pattern;
            unsigned flags;
            Scanner scanner;
        };
        static constexpr std::size_t COMPILE_CACHE_MAX = 8;
        std::list<CompiledEntry> compile_cache_;

        Scanner &get_or_compile(const std::string &pattern, unsigned flags);
    };

} // namespace kestrel
