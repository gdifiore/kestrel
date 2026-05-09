#include "kestrel/line_index.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <future>
#include <span>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace kestrel
{

    namespace
    {
        constexpr std::size_t PARALLEL_THRESHOLD = 4 * 1024 * 1024; // 4 MB

        // Newline offsets within [data, data+size), returned relative to chunk start.
        std::vector<std::size_t> scan_chunk(const char *data, std::size_t size)
        {
            std::vector<std::size_t> out;
            out.reserve(size / 100 + 1);
#if defined(__x86_64__) || defined(_M_X64)
            const __m256i nl = _mm256_set1_epi8('\n');
            std::size_t i = 0;
            for (; i + 32 <= size; i += 32)
            {
                __m256i c = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data + i));
                uint32_t m = _mm256_movemask_epi8(_mm256_cmpeq_epi8(c, nl));
                while (m)
                {
                    out.push_back(i + __builtin_ctz(m) + 1);
                    m &= m - 1;
                }
            }
            for (; i < size; ++i)
                if (data[i] == '\n')
                    out.push_back(i + 1);
#else
            for (std::size_t i = 0; i < size; ++i)
                if (data[i] == '\n')
                    out.push_back(i + 1);
#endif
            return out;
        }
    }

    LineIndex::LineIndex(std::span<const char> buf)
        : buf_size_(buf.size())
    {
        line_starts_.push_back(0);

        // Reserve space - estimate ~100 chars per line for better performance
        line_starts_.reserve(buf.size() / 100 + 1);

        scan_for_newlines(buf);

        // Trim phantom trailing line when buffer ends with '\n'.
        if (line_starts_.size() > 1 && line_starts_.back() == buf_size_)
            line_starts_.pop_back();
    }

    void LineIndex::scan_for_newlines(std::span<const char> buf)
    {
        if (buf.size() >= PARALLEL_THRESHOLD)
        {
            scan_for_newlines_parallel(buf);
            return;
        }
        std::vector<std::size_t> offs = scan_chunk(buf.data(), buf.size());
        line_starts_.reserve(line_starts_.size() + offs.size());
        line_starts_.insert(line_starts_.end(), offs.begin(), offs.end());
    }

    void LineIndex::scan_for_newlines_parallel(std::span<const char> buf)
    {
        unsigned n = std::thread::hardware_concurrency();
        if (n == 0)
            n = 2;
        if (n > 8)
            n = 8;

        const std::size_t chunk = buf.size() / n;

        std::vector<std::future<std::vector<std::size_t>>> futs;
        futs.reserve(n);
        for (unsigned t = 0; t < n; ++t)
        {
            const std::size_t start = t * chunk;
            const std::size_t end = (t == n - 1) ? buf.size() : start + chunk;
            futs.push_back(std::async(std::launch::async,
                                      scan_chunk, buf.data() + start, end - start));
        }

        std::vector<std::vector<std::size_t>> parts(n);
        std::size_t total = line_starts_.size();
        for (unsigned t = 0; t < n; ++t)
        {
            parts[t] = futs[t].get();
            total += parts[t].size();
        }
        line_starts_.reserve(total);

        for (unsigned t = 0; t < n; ++t)
        {
            const std::size_t base = t * chunk;
            for (std::size_t off : parts[t])
                line_starts_.push_back(base + off);
        }
    }

    std::size_t LineIndex::line_of(std::size_t offset) const
    {
#ifndef NDEBUG
        assert(offset < buf_size_ && "offset out of range");
#endif
        auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
        return std::distance(line_starts_.begin(), it) - 1;
    }

} // namespace kestrel
