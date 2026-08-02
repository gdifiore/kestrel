#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
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

        // Scans in bounded streaming chunks, so cancellation is observed even
        // when the pattern has no matches. When match_limit is reached the scan
        // stops early and sets *truncated (when supplied).
        std::vector<Match> scan(std::string_view buf,
                                const std::atomic<uint64_t> *cancel_counter = nullptr,
                                uint64_t my_gen = 0,
                                std::size_t match_limit = std::numeric_limits<std::size_t>::max(),
                                bool *truncated = nullptr) const;

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
