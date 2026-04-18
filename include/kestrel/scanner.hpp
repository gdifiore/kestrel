#pragma once

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

        std::vector<Match> scan(std::string_view buf) const;

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
