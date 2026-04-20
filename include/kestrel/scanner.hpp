#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

#include <hs.h>

namespace kestrel
{

    struct Match
    {
        size_t start, end;

        bool operator<(const Match &m) const
        {
            return start < m.start;
        }
    };

    class Scanner
    {
    public:
        explicit Scanner(std::string_view pattern, unsigned flags = 0);
        ~Scanner();
        Scanner(Scanner &&obj) noexcept;
        Scanner &operator=(Scanner &&obj) noexcept;
        Scanner(const Scanner &obj) = delete;

        // If cancel_counter is non-null, the match callback polls it each hit and
        // aborts the scan when its value differs from my_gen. Used to stop a stale
        // background scan when a newer pattern supersedes it.
        std::vector<Match> scan(std::string_view buf,
                                const std::atomic<uint64_t> *cancel_counter = nullptr,
                                uint64_t my_gen = 0) const;

    private:
        hs_database_t *db_ = nullptr;
        hs_scratch_t *scratch_ = nullptr;
    };

    class ScannerError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };
} // namespace kestrel
