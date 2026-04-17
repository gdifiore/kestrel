#include "kestrel/app.hpp"
#include "kestrel/search.hpp"
#include "kestrel/ui.hpp"
#include "kestrel/util.hpp"
#include "kestrel/window.hpp"

#include <GLFW/glfw3.h>
#include <hs.h>

#include <iostream>

namespace kestrel
{

    int run_app(int argc, char **argv)
    {
        auto args = parse_cli(argc, argv, std::cerr);
        if (!args)
            return 1;
        if (args->show_help)
        {
            print_usage(std::cout, argc > 0 ? argv[0] : "kestrel");
            return 0;
        }

        SearchController search;
        auto load_path = [&](std::string_view p) -> bool {
            if (!is_valid_file_path(p)) return false;
            try { search.load_source(p); }
            catch (const SourceError&) { return false; }
            return true;
        };

        if (args->file_path && !load_path(*args->file_path))
            return 1;

        Window w("kestrel", 800, 600);
        w.on_file_drop([&](std::span<const char *> paths) {
            if (!paths.empty()) load_path(paths[0]);
        });

        UiState ui;
        while (!w.should_close() && !ui.quit_requested)
        {
            // UI intent -> controller
            unsigned flags = HS_FLAG_SOM_LEFTMOST;
            if (!ui.case_sensitive) flags |= HS_FLAG_CASELESS;
            search.set_pattern(ui.query, flags);
            search.tick(glfwGetTime());

            // controller -> UI
            if (search.has_source()) {
                ui.source_bytes = search.source_bytes();
                ui.lines = &search.line_index();
            } else {
                ui.source_bytes = {};
                ui.lines = nullptr;
            }
            ui.pattern_active  = !search.pattern_empty();
            ui.visible_lines   = &search.matched_lines();
            ui.compile_error   = search.compile_error();
            ui.match_count     = search.matches().size();
            ui.scan_ms         = search.last_scan_ms();
            ui.matches_before  = static_cast<int>(search.matches_before(0));
            ui.matches_after   = static_cast<int>(search.matches_after(0));

            w.begin_frame();
            draw_ui(ui);
            if (!ui.pending_open.empty()) {
                load_path(ui.pending_open);
                ui.pending_open.clear();
            }
            w.end_frame();
        }

        return 0;
    }

} // namespace kestrel
