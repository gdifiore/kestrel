#include "kestrel/line_index.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace kestrel
{

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
#if defined(__x86_64__) || defined(_M_X64)
        if (buf.size() >= 32) {
            scan_for_newlines_simd(buf);
            return;
        }
#endif
        // Fallback to scalar scan
        for (std::size_t i = 0; i < buf.size(); ++i)
            if (buf[i] == '\n')
                line_starts_.push_back(i + 1);
    }

#if defined(__x86_64__) || defined(_M_X64)
    void LineIndex::scan_for_newlines_simd(std::span<const char> buf)
    {
        const char* data = buf.data();
        std::size_t size = buf.size();
        const __m256i newline_vec = _mm256_set1_epi8('\n');

        std::size_t i = 0;
        // Process 32 bytes at a time with AVX2
        for (; i + 32 <= size; i += 32) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
            __m256i cmp = _mm256_cmpeq_epi8(chunk, newline_vec);
            uint32_t mask = _mm256_movemask_epi8(cmp);

            // Process each bit in mask
            while (mask) {
                int bit_pos = __builtin_ctz(mask);
                line_starts_.push_back(i + bit_pos + 1);
                mask &= mask - 1; // Clear lowest set bit
            }
        }

        // Handle remaining bytes with scalar scan
        for (; i < size; ++i)
            if (data[i] == '\n')
                line_starts_.push_back(i + 1);
    }
#endif

    std::size_t LineIndex::line_of(std::size_t offset) const
    {
#ifndef NDEBUG
        assert(offset < buf_size_ && "offset out of range");
#endif
        auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
        return std::distance(line_starts_.begin(), it) - 1;
    }

} // namespace kestrel
