#include "kestrel/ui.hpp"
#include "ui_internal.hpp"

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

} // namespace kestrel
