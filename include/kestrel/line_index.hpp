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

        // Ordered starts, exposed for linear bulk offset-to-line conversion.
        std::span<const std::size_t> line_starts() const noexcept { return line_starts_; }

        // Extend an existing index after an append. The caller supplies the
        // complete new buffer, whose existing prefix is unchanged.
        void append(std::span<const char> full_buf);

    private:
        void scan_for_newlines(std::span<const char> buf);
        void scan_for_newlines_parallel(std::span<const char> buf);

        std::vector<std::size_t> line_starts_;
        std::size_t buf_size_ = 0;
        bool ends_with_newline_ = false;
    };

} // namespace kestrel
