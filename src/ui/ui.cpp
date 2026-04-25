#include "kestrel/ui.hpp"
#include "ui_internal.hpp"

namespace kestrel
{

    void draw_ui(UiInputs &in, const SearchController &search)
    {
        draw_main_menu(in);
        draw_toolbar_row(in, search);
        draw_search_bar(in, search);
        update_view_cache(in, search);
        draw_status_bar(in, search);
        draw_results(in, search);
        draw_minimap(in, search);
        draw_settings_popup(in);
        draw_open_dialog(in);
        draw_goto_line_dialog(in, search);
    }

} // namespace kestrel
