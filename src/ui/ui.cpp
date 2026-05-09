#include "kestrel/ui.hpp"
#include "kestrel/search.hpp"
#include "ui_internal.hpp"

#include <algorithm>

namespace kestrel
{

    void draw_ui(UiInputs &in, const SearchController &search)
    {
        // Order matters: each panel below reads layout heights set by the
        // panels above. Drawing producers before consumers avoids 1-frame
        // lag that makes the UI jitter while the window is being resized.
        draw_main_menu(in);
        draw_search_bar(in, search);   // sets search_bar_h
        draw_toolbar_row(in, search);  // uses search_bar_h, sets toolbar_h
        draw_status_bar(in, search);   // sets status_bar_h
        update_view_cache(in, search);
        draw_results(in, search);      // uses all three
        draw_minimap(in, search);      // uses all three
        draw_settings_popup(in);
        draw_open_dialog(in);
        draw_goto_line_dialog(in, search);
    }

    int ViewIndex::row_count() const noexcept
    {
        if (use_custom) return static_cast<int>(view_lines.size());
        if (filter_view) return static_cast<int>(matched.size());
        return total_lines;
    }

    size_t ViewIndex::row_to_source(int row) const
    {
        if (use_custom) return view_lines[static_cast<size_t>(row)];
        if (filter_view) return matched[static_cast<size_t>(row)];
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
