#include "kestrel/app.hpp"
#include "kestrel/ui.hpp"
#include "kestrel/util.hpp"
#include "kestrel/source.hpp"
#include "kestrel/window.hpp"

#include <iostream>
#include <optional>

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

        std::optional<Source> s;
        auto load_path = [&](std::string_view p) -> bool {
            if (!is_valid_file_path(p)) {
                std::cerr << "invalid: " << p << "\n";
                return false;
            }
            try { s.emplace(Source::from_path(p)); }
            catch (const SourceError& e) {
                std::cerr << "error: " << e.what() << "\n";
                return false;
            }
            std::cout << "loaded file: " << p << "\n";
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
