#include "kestrel/app.hpp"
#include "kestrel/config.hpp"
#include "kestrel/search.hpp"
#include "kestrel/ui.hpp"
#include "kestrel/util.hpp"
#include "kestrel/window.hpp"

#include <GLFW/glfw3.h>
#include <hs.h>
#include <spdlog/spdlog.h>

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

        spdlog::info("kestrel start");

        SearchController search;
        auto load_path = [&](std::string_view p) -> bool {
            if (!is_valid_file_path(p)) {
                spdlog::warn("reject path: {}", p);
                return false;
            }
            try { search.load_source(p); }
            catch (const SourceError& e) {
                spdlog::error("load_source failed: {} ({})", p, e.what());
                return false;
            }
            spdlog::info("loaded {} ({} bytes)", p, search.source_bytes().size());
            return true;
        };

        if (args->file_path && !load_path(*args->file_path))
            return 1;

        Window w("kestrel", 800, 600);
        w.on_file_drop([&](std::span<const char *> paths) {
            if (!paths.empty()) load_path(paths[0]);
        });

        UiInputs ui;
        load_config(ui);
        while (!w.should_close() && !ui.quit_requested)
        {
            unsigned flags = 0;
            if (!ui.case_sensitive) flags |= HS_FLAG_CASELESS;
            search.set_pattern(ui.query, flags);
            search.tick(glfwGetTime());

            ui.matches_before = static_cast<int>(search.matches_before(0));
            ui.matches_after  = static_cast<int>(search.matches_after(0));

            w.begin_frame();
            draw_ui(ui, search);
            if (ui.pending_open) {
                load_path(*ui.pending_open);
                ui.pending_open.reset();
            }
            w.end_frame();
        }

        save_config(ui);
        spdlog::info("kestrel exit");
        return 0;
    }

} // namespace kestrel
