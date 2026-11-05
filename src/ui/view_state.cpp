#include "ui_internal.hpp"
#include "kestrel/search.hpp"

#include <algorithm>

namespace kestrel
{
    namespace
    {
        void synchronize_time_filter(ViewFilters::Time &time,
                                     const SearchController &search)
        {
            if (!search.has_source())
            {
                time.source_initialized = false;
                return;
            }
            const auto &timestamps = search.timestamp_index();
            if (timestamps.empty())
            {
                // A timestamp-free follow snapshot must not discard a narrowed
                // selection before timestamps reappear in a later refresh.
                if (time.source_revision != search.source_revision())
                    time.source_initialized = false;
                return;
            }
            const bool new_source = !time.source_initialized ||
                                    time.source_revision != search.source_revision();
            const bool full_range = time.start == time.source_min &&
                                    time.end == time.source_max;
            // Follow extends a full-range selection, but preserves an interval
            // the user deliberately narrowed, even across rotation or truncation.
            if (new_source || full_range)
            {
                time.start = timestamps.min_ts();
                time.end = timestamps.max_ts();
            }
            time.source_min = timestamps.min_ts();
            time.source_max = timestamps.max_ts();
            time.source_revision = search.source_revision();
            time.source_initialized = true;
        }
    }

    // Build/refresh in.layout.view_lines once per frame. Skips work when none
    // of the inputs (time filter range/state, regex filter view, completed
    // scan generation) changed since last frame.
    void update_view_cache(UiInputs &in, const SearchController &search)
    {
        if (!search.has_source())
        {
            in.layout.view_has_custom = false;
            in.layout.view_lines.clear();
            in.layout.view_cache_completed_gen = 0;
            return;
        }
        const int total_lines = static_cast<int>(search.line_index().line_count());
        const auto &matched = search.matched_lines();
        const bool filtered = !search.pattern_empty();
        const bool filter_view = in.view.display_only_filtered_lines && filtered;

        // Resolve which filters are active this frame. New filters: append a
        // (state, predicate) pair below + a comparison line in `same`.
        const auto &tf = in.layout.filters.time;
        const bool time_active = tf.active && !search.timestamp_index().empty();
        const bool needs_custom = time_active; // filter_view alone reuses matched directly

        if (!needs_custom)
        {
            in.layout.view_has_custom = false;
            in.layout.view_lines.clear();
            in.layout.view_cache_completed_gen = 0;
            return;
        }

        const uint64_t gen = search.completed_generation();
        const bool same =
            in.layout.view_has_custom &&
            in.layout.view_cache_completed_gen == gen &&
            in.layout.view_cache_time_filter == time_active &&
            in.layout.view_cache_filter_view == filter_view &&
            in.layout.view_cache_time_start == tf.start &&
            in.layout.view_cache_time_end == tf.end;
        if (same)
        {
            return;
        }

        const TimestampIndex &ts = search.timestamp_index();
        auto pass = [&](size_t line)
        {
            // AND of all active filter predicates.
            if (time_active)
            {
                int64_t t = ts.at(line);
                if (t == TimestampIndex::kNone || t < tf.start || t > tf.end)
                {
                    return false;
                }
            }
            return true;
        };
        in.layout.view_lines.clear();
        if (filter_view)
        {
            in.layout.view_lines.reserve(matched.size());
            for (size_t m : matched)
            {
                if (pass(m))
                {
                    in.layout.view_lines.push_back(m);
                }
            }
        }
        else
        {
            in.layout.view_lines.reserve(static_cast<size_t>(total_lines));
            for (size_t i = 0; i < static_cast<size_t>(total_lines); i++)
            {
                if (pass(i))
                {
                    in.layout.view_lines.push_back(i);
                }
            }
        }
        in.layout.view_has_custom = true;
        in.layout.view_cache_completed_gen = gen;
        in.layout.view_cache_time_filter = time_active;
        in.layout.view_cache_filter_view = filter_view;
        in.layout.view_cache_time_start = tf.start;
        in.layout.view_cache_time_end = tf.end;
        ++in.layout.view_revision;
    }

    void prepare_view_state(UiInputs &in, const SearchController &search)
    {
        synchronize_time_filter(in.layout.filters.time, search);
        update_view_cache(in, search);
        if (!search.has_source())
        {
            in.cursor.line = 0;
            in.cursor.offset = 0;
            in.selection.anchor_line.reset();
            return;
        }
        const auto &lines = search.line_index();
        const std::size_t count = lines.line_count();
        in.cursor.line = count ? std::min(in.cursor.line, count - 1) : 0;
        in.cursor.offset = count ? lines.line_start(in.cursor.line) : 0;
        if (in.selection.anchor_line && *in.selection.anchor_line >= count)
            in.selection.anchor_line.reset();
    }

    int ViewIndex::row_count() const noexcept
    {
        if (use_custom)
            return static_cast<int>(view_lines.size());
        if (filter_view)
            return static_cast<int>(matched.size());
        return total_lines;
    }

    size_t ViewIndex::row_to_source(int row) const
    {
        if (use_custom)
            return view_lines[static_cast<size_t>(row)];
        if (filter_view)
            return matched[static_cast<size_t>(row)];
        return static_cast<size_t>(row);
    }

    int ViewIndex::source_to_row(size_t source_line) const
    {
        if (use_custom)
        {
            auto it = std::lower_bound(view_lines.begin(), view_lines.end(), source_line);
            if (it != view_lines.end() && *it == source_line)
                return static_cast<int>(it - view_lines.begin());
            return -1;
        }
        if (filter_view)
        {
            auto it = std::lower_bound(matched.begin(), matched.end(), source_line);
            if (it != matched.end() && *it == source_line)
                return static_cast<int>(it - matched.begin());
            return -1;
        }
        return static_cast<int>(source_line);
    }

    ViewIndex make_view_index(const UiInputs &in, const SearchController &search)
    {
        const bool filtered = !search.pattern_empty();
        return ViewIndex{
            in.layout.view_lines,
            search.matched_lines(),
            static_cast<int>(search.has_source() ? search.line_index().line_count() : 0),
            in.layout.view_has_custom,
            in.view.display_only_filtered_lines && filtered,
        };
    }

} // namespace kestrel
