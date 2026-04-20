#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace kestrel
{

    class LineIndex
    {
    public:
        explicit LineIndex(std::span<const char> buf);

        // 0-indexed line number containing byte `offset`.
        // Precondition: offset <= buffer size used to build the index.
        std::size_t line_of(std::size_t offset) const;

        std::size_t line_count() const noexcept { return line_starts_.size(); }

        // Byte offset where line `line` begins.
        std::size_t line_start(std::size_t line) const { return line_starts_[line]; }

    private:
        void scan_for_newlines(std::span<const char> buf);

#if defined(__x86_64__) || defined(_M_X64)
        void scan_for_newlines_simd(std::span<const char> buf);
#endif

        std::vector<std::size_t> line_starts_;
        std::size_t buf_size_ = 0;
    };

} // namespace kestrel
