#include "kestrel/scanner.hpp"

#include <spdlog/spdlog.h>

#include <hs.h>
#include <algorithm>
#include <cstddef>
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
            std::size_t match_limit;
            bool cancelled = false;
            bool truncated = false;
        };

        constexpr std::size_t SCAN_CHUNK_BYTES = 16 * 1024 * 1024;

        int on_match(unsigned int /*id*/,
                     unsigned long long from,
                     unsigned long long to,
                     unsigned int /*flags*/,
                     void *ctx)
        {
            auto *c = static_cast<ScanCtx *>(ctx);
            if (c->cancelled || c->truncated)
                return 1;
            if (c->cancel_counter &&
                c->cancel_counter->load(std::memory_order_relaxed) != c->my_gen)
            {
                c->cancelled = true;
                return 1; // abort scan
            }
            if (c->out->size() >= c->match_limit)
            {
                c->truncated = true;
                return 1;
            }
            c->out->push_back({from, to});
            return 0;
        }
    }

    Scanner::Scanner(std::string_view pattern, unsigned flags)
    {
        std::string pat(pattern);
        flags |= HS_FLAG_SOM_LEFTMOST;

        hs_compile_error_t *cerr = nullptr;
        // Streaming mode preserves whole-file regex semantics across bounded
        // chunks while giving the caller cancellation checkpoints even when a
        // scan produces no callbacks. The large SOM horizon retains exact byte
        // offsets for matches spanning distant chunks.
        const unsigned mode = HS_MODE_STREAM | HS_MODE_SOM_HORIZON_LARGE;
        hs_error_t rc = hs_compile(pat.c_str(), flags, mode, nullptr, &db_, &cerr);

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
                                     uint64_t my_gen,
                                     std::size_t match_limit,
                                     bool *truncated) const
    {
        std::vector<Match> out;
        // Pre-reserve based on buffer size - estimate 1 match per 10KB for typical patterns
        out.reserve(std::min(buf.size() / 10240 + 100, match_limit));
        ScanCtx ctx{&out, cancel_counter, my_gen, match_limit};

        hs_stream_t *stream = nullptr;
        hs_error_t rc = hs_open_stream(db_, 0, &stream);
        if (rc != HS_SUCCESS)
            throw ScannerError("hs_open_stream failed: rc=" + std::to_string(rc));

        for (std::size_t offset = 0; offset < buf.size();)
        {
            if (cancel_counter && cancel_counter->load(std::memory_order_relaxed) != my_gen)
            {
                ctx.cancelled = true;
                break;
            }
            const auto length = static_cast<unsigned int>(
                std::min(SCAN_CHUNK_BYTES, buf.size() - offset));
            rc = hs_scan_stream(stream, buf.data() + offset, length, 0,
                                scratch_, on_match, &ctx);
            offset += length;
            if (rc == HS_SCAN_TERMINATED)
                break;
            if (rc != HS_SUCCESS)
            {
                hs_close_stream(stream, scratch_, on_match, &ctx);
                throw ScannerError("hs_scan_stream failed: rc=" + std::to_string(rc));
            }
        }

        const hs_error_t close_rc = hs_close_stream(stream, scratch_, on_match, &ctx);
        if (close_rc != HS_SUCCESS && close_rc != HS_SCAN_TERMINATED)
            throw ScannerError("hs_close_stream failed: rc=" + std::to_string(close_rc));

        if (truncated)
            *truncated = ctx.truncated;
        return out;
    }

} // namespace kestrel
