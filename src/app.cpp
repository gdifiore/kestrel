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

    static void handle_cursor_input(UiInputs &ui, const SearchController &search)
    {
        if (!search.has_source())
            return;

        const auto &lines = search.line_index();
        size_t max_line = lines.line_count() - 1;

        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            ui.cursor_line = (ui.cursor_line > 0) ? ui.cursor_line - 1 : 0;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            ui.cursor_line = std::min(ui.cursor_line + 1, max_line);
        if (ImGui::IsKeyPressed(ImGuiKey_Home))
            ui.cursor_line = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_End))
            ui.cursor_line = max_line - 1;

        // Rough estimate of visible lines for Page Up/Down
        float line_height = ImGui::GetTextLineHeightWithSpacing();
        float viewport_height = ImGui::GetMainViewport()->WorkSize.y;
        int visible_lines = std::max(10, (int)((viewport_height * 0.6f) / line_height));

        if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
            ui.cursor_line = std::max((int)ui.cursor_line - visible_lines, 0);
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
            ui.cursor_line = std::min(ui.cursor_line + visible_lines, max_line);

        // Update offset + match counts
        ui.cursor_offset = lines.line_start(ui.cursor_line);
        ui.matches_before = search.matches_before(ui.cursor_offset);
        ui.matches_after = search.matches_after(ui.cursor_offset);
    }

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
        auto load_path = [&](std::string_view p) -> bool
        {
            if (!is_valid_file_path(p))
            {
                spdlog::warn("reject path: {}", p);
                return false;
            }
            try
            {
                search.load_source(p);
            }
            catch (const SourceError &e)
            {
                spdlog::error("load_source failed: {} ({})", p, e.what());
                return false;
            }
            spdlog::info("loaded {} ({} bytes)", p, search.source_bytes().size());
            return true;
        };

        if (args->file_path && !load_path(*args->file_path))
            return 1;

        Window w("kestrel", 800, 600);
        w.on_file_drop([&](std::span<const char *> paths)
                       {
            if (!paths.empty()) load_path(paths[0]); });

        UiInputs ui;
        load_config(ui);
        while (!w.should_close() && !ui.quit_requested)
        {
            unsigned flags = 0;
            if (!ui.case_sensitive)
                flags |= HS_FLAG_CASELESS;
            if (ui.dotall)
                flags |= HS_FLAG_DOTALL;
            if (ui.multiline)
                flags |= HS_FLAG_MULTILINE;

            search.set_pattern(ui.query, flags);
            search.tick(glfwGetTime());

            handle_cursor_input(ui, search);

            ui.matches_before = static_cast<int>(search.matches_before(0));
            ui.matches_after = static_cast<int>(search.matches_after(0));

            w.begin_frame();
            draw_ui(ui, search);
            if (ui.pending_open)
            {
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
