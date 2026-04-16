#include "kestrel/scanner.hpp"

#include <hs.h>
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>

namespace kestrel
{

    namespace {
        int on_match(unsigned int /*id*/,
                     unsigned long long from,
                     unsigned long long to,
                     unsigned int /*flags*/,
                     void *ctx)
        {
            auto *out = static_cast<std::vector<Match> *>(ctx);
            out->push_back({from, to});
            return 0;
        }
    }

    Scanner::Scanner(std::string_view pattern, unsigned flags)
    {
        std::string pat(pattern);
        flags |= HS_FLAG_SOM_LEFTMOST;

        hs_compile_error_t *cerr = nullptr;
        hs_error_t rc = hs_compile(pat.c_str(), flags, HS_MODE_BLOCK, nullptr, &db_, &cerr);

        if (rc != HS_SUCCESS)
        {
            std::string msg = cerr ? cerr->message : "unknown";
            hs_free_compile_error(cerr);
            throw ScannerError("hs_compile: " + msg);
        }

        rc = hs_alloc_scratch(db_, &scratch_);
        if (rc != HS_SUCCESS)
        {
            hs_free_database(db_);
            db_ = nullptr;
            throw ScannerError("hs_alloc_scratch failed");
        }
    }

    Scanner::~Scanner()
    {
        hs_free_scratch(scratch_);
        hs_free_database(db_);
    }

    Scanner::Scanner(Scanner &&obj) noexcept : db_(obj.db_), scratch_(obj.scratch_)
    {
        obj.db_ = nullptr;
        obj.scratch_ = nullptr;
    }

    Scanner &Scanner::operator=(Scanner &&obj) noexcept
    {
        std::swap(db_, obj.db_);
        std::swap(scratch_, obj.scratch_);
        return *this;
    }

    std::vector<Match> Scanner::scan(std::string_view buf) const
    {
        std::vector<Match> out;

        hs_error_t rc = hs_scan(db_, buf.data(), buf.size(), 0, scratch_, on_match, &out);

        if (rc != HS_SUCCESS)
        {
            throw ScannerError("hs_scan failed: rc=" + std::to_string(rc));
        }
        return out;
    }

} // namespace kestrel
