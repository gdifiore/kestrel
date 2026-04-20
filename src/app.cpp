#include "kestrel/app.hpp"
#include "kestrel/config.hpp"
#include "kestrel/search.hpp"
#include "kestrel/ui.hpp"
#include "kestrel/util.hpp"
#include "kestrel/window.hpp"

#include <GLFW/glfw3.h>
#include <hs.h>
#include <spdlog/spdlog.h>

#include <algorithm>
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

    static void handle_keyboard_shortcuts(UiInputs &ui, const SearchController &search)
    {
        const ImGuiIO &io = ImGui::GetIO();

        // Ctrl+F - Focus search input
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F))
        {
            ui.focus_search = true;
        }

        // Ctrl+O - Open file dialog
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
        {
            ui.trigger_open_dialog = true;
        }

        // Ctrl+Q - Quit application
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q))
        {
            ui.quit_requested = true;
        }

        // Escape - Clear search input or unfocus search box
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            if (ImGui::IsAnyItemActive())
            {
                // If search input is focused, unfocus it
                ImGui::SetKeyboardFocusHere(-1); // Unfocus current item
            }
            else
            {
                // Otherwise clear search
                ui.query[0] = '\0';
            }
        }

        // n - Go to next match (only when no input widget is focused)
        if (ImGui::IsKeyPressed(ImGuiKey_N) && !io.KeyShift && !ImGui::IsAnyItemActive())
        {
            if (search.has_source() && !search.matches().empty())
            {
                const auto &matches = search.matches();
                auto cursor_offset = search.line_index().line_start(ui.cursor_line);

                // Find next match after cursor
                auto it = std::upper_bound(matches.begin(), matches.end(), cursor_offset,
                                           [](size_t offset, const Match &m)
                                           { return offset < m.start; });

                if (it != matches.end())
                {
                    // Found next match
                    ui.cursor_line = search.line_index().line_of(it->start);
                }
                else if (!matches.empty())
                {
                    // Wrap to first match
                    ui.cursor_line = search.line_index().line_of(matches[0].start);
                }
            }
        }

        // Shift+N - Go to previous match (only when no input widget is focused)
        if (ImGui::IsKeyPressed(ImGuiKey_N) && io.KeyShift && !ImGui::IsAnyItemActive())
        {
            if (search.has_source() && !search.matches().empty())
            {
                const auto &matches = search.matches();
                auto cursor_offset = search.line_index().line_start(ui.cursor_line);

                // Find previous match before cursor
                auto it = std::lower_bound(matches.begin(), matches.end(), cursor_offset,
                                           [](const Match &m, size_t offset)
                                           { return m.start < offset; });

                if (it != matches.begin())
                {
                    // Found previous match
                    --it;
                    ui.cursor_line = search.line_index().line_of(it->start);
                }
                else if (!matches.empty())
                {
                    // Wrap to last match
                    ui.cursor_line = search.line_index().line_of(matches.back().start);
                }
            }
        }
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
        UiInputs ui;

        auto load_path = [&](std::string_view p) -> bool
        {
            if (!is_valid_file_path(p))
            {
                spdlog::warn("reject path: {}", p);
                ui.loading_error = "Invalid file path";
                return false;
            }

            // Start async loading
            ui.loading = true;
            ui.loading_path = p;
            ui.loading_error.clear();
            search.load_source_async(p);
            return true;
        };

        if (args->file_path && !load_path(*args->file_path))
            return 1;

        Window w("kestrel", 800, 600);
        w.on_file_drop([&](std::span<const char *> paths)
                       {
        if (!paths.empty()) load_path(paths[0]); });
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

            // Update loading state from SearchController
            bool was_loading = ui.loading;
            ui.loading = search.is_loading();

            if (was_loading && !ui.loading)
            {
                // Loading just finished
                std::string error = search.get_loading_error();
                if (!error.empty())
                {
                    ui.loading_error = error;
                }
                else if (search.has_source())
                {
                    // Successfully loaded - add to recent files
                    add_recent_file(ui, ui.loading_path);
                    ui.loading_error.clear();
                }
            }

            handle_cursor_input(ui, search);
            handle_keyboard_shortcuts(ui, search);

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
