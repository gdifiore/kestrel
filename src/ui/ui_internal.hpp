#pragma once

#include "kestrel/ui.hpp"

#include <vector>
#include <cstddef>

namespace kestrel
{
    class SearchController;

    // Per-window draw entry points. Each lives in its own TU.
    void draw_main_menu(UiInputs &in);
    void draw_search_bar(UiInputs &in, const SearchController &search);
    void draw_toolbar_row(UiInputs &in, const SearchController &search);
    void update_view_cache(UiInputs &in, const SearchController &search);
    void draw_results(UiInputs &in, const SearchController &search);
    void draw_minimap(UiInputs &in, const SearchController &search);
    void draw_settings_popup(UiInputs &in);
    void draw_open_dialog(UiInputs &in);
    void draw_goto_line_dialog(UiInputs &in, const SearchController &search);

    // Shared helper. Maps a source line to its display row in the current view.
    // Returns -1 if the line is filtered out. Used by results (autoscroll) and
    // minimap (cursor row + match marks).
    int source_to_display_row(const UiInputs &in,
                              const std::vector<size_t> &matched,
                              bool filter_view, size_t source_line);

} // namespace kestrel
