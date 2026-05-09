#include "kestrel/scanner.hpp"

#include <spdlog/spdlog.h>

#include <hs.h>
#include <climits>
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>

namespace kestrel
{

    namespace
    {
        struct ScanCtx
        {
            std::vector<Match> *out;
            const std::atomic<uint64_t> *cancel_counter;
            uint64_t my_gen;
        };

        int on_match(unsigned int /*id*/,
                     unsigned long long from,
                     unsigned long long to,
                     unsigned int /*flags*/,
                     void *ctx)
        {
            auto *c = static_cast<ScanCtx *>(ctx);
            if (c->cancel_counter &&
                c->cancel_counter->load(std::memory_order_relaxed) != c->my_gen)
                return 1; // abort scan
            c->out->push_back({from, to});
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
            throw ScannerError("hs_alloc_scratch failed: rc=" + std::to_string(rc));
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

    std::vector<Match> Scanner::scan(std::string_view buf,
                                     const std::atomic<uint64_t> *cancel_counter,
                                     uint64_t my_gen) const
    {
        std::vector<Match> out;
        // Pre-reserve based on buffer size - estimate 1 match per 10KB for typical patterns
        out.reserve(buf.size() / 10240 + 100);
        ScanCtx ctx{&out, cancel_counter, my_gen};

        // hs_scan's length is unsigned int (4 GB cap). Clip and warn so larger
        // inputs fail visibly instead of silently truncating via narrowing.
        // TODO: replace with hs_scan_vector to handle >4 GB inputs.
        std::size_t len = buf.size();
        if (len > UINT_MAX)
        {
            spdlog::warn("scan input {} bytes exceeds hs_scan 4 GB limit; "
                         "scanning first {} bytes only",
                         len, UINT_MAX);
            len = UINT_MAX;
        }

        hs_error_t rc = hs_scan(db_, buf.data(), static_cast<unsigned int>(len),
                                0, scratch_, on_match, &ctx);

        // HS_SCAN_TERMINATED means our callback returned non-zero (cancellation).
        // Treat as a normal early return; caller will discard stale results.
        if (rc != HS_SUCCESS && rc != HS_SCAN_TERMINATED)
        {
            throw ScannerError("hs_scan failed: rc=" + std::to_string(rc));
        }
        return out;
    }

} // namespace kestrel
