#pragma once

#include "kestrel/ui.hpp"

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
    void draw_status_bar(UiInputs &in, const SearchController &search);
    void draw_settings_popup(UiInputs &in);
    void draw_open_dialog(UiInputs &in);
    void draw_goto_line_dialog(UiInputs &in, const SearchController &search);

} // namespace kestrel
