#include "kestrel/line_index.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <span>

namespace kestrel
{

    LineIndex::LineIndex(std::span<const char> buf)
        : buf_size_(buf.size())
    {
        line_starts_.push_back(0);
        // profile this later, see if SIMD is needed
        for (std::size_t i = 0; i < buf.size(); ++i)
            if (buf[i] == '\n')
                line_starts_.push_back(i + 1);

        // Trim phantom trailing line when buffer ends with '\n'.
        if (line_starts_.size() > 1 && line_starts_.back() == buf_size_)
            line_starts_.pop_back();
    }

    std::size_t LineIndex::line_of(std::size_t offset) const
    {
        assert(offset < buf_size_ && "offset out of range");
        auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
        return std::distance(line_starts_.begin(), it) - 1;
    }

} // namespace kestrel
