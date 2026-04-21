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
#include <cstring>
#include <iostream>

namespace kestrel
{

    static void handle_cursor_input(UiInputs &ui, const SearchController &search)
    {
        if (!search.has_source())
            return;

        const auto &lines = search.line_index();
        size_t max_line = lines.line_count() - 1;

        auto &cur = ui.cursor;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            cur.line = (cur.line > 0) ? cur.line - 1 : 0;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            cur.line = std::min(cur.line + 1, max_line);
        if (ImGui::IsKeyPressed(ImGuiKey_Home))
            cur.line = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_End))
            cur.line = max_line - 1;

        // Rough estimate of visible lines for Page Up/Down
        float line_height = ImGui::GetTextLineHeightWithSpacing();
        float viewport_height = ImGui::GetMainViewport()->WorkSize.y;
        int visible_lines = std::max(10, (int)((viewport_height * 0.6f) / line_height));

        if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
            cur.line = std::max((int)cur.line - visible_lines, 0);
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
            cur.line = std::min(cur.line + visible_lines, max_line);

        // Update offset + match counts
        cur.offset = lines.line_start(cur.line);
        ui.layout.matches_before = search.matches_before(cur.offset);
        ui.layout.matches_after = search.matches_after(cur.offset);
    }

    static void handle_keyboard_shortcuts(UiInputs &ui, const SearchController &search)
    {
        const ImGuiIO &io = ImGui::GetIO();

        // Ctrl+F - Focus search input
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F))
        {
            ui.hotkeys.focus_search = true;
        }

        // Ctrl+O - Open file dialog
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
        {
            ui.hotkeys.trigger_open_dialog = true;
        }

        // Ctrl+G - Go to line
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G))
        {
            ui.hotkeys.show_goto_line = true;
        }

        // Ctrl+C - Copy current search pattern (when no input widget is focused)
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && !ImGui::IsAnyItemActive())
        {
            ImGui::SetClipboardText(ui.search.query);
        }

        // Ctrl+V - Paste to search pattern (when no input widget is focused)
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !ImGui::IsAnyItemActive())
        {
            const char* clipboard = ImGui::GetClipboardText();
            if (clipboard && clipboard[0] != '\0')
            {
                strncpy(ui.search.query, clipboard, sizeof(ui.search.query) - 1);
                ui.search.query[sizeof(ui.search.query) - 1] = '\0';
            }
        }

        // Ctrl+Q - Quit application
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q))
        {
            ui.quit_requested = true;
        }

        // Escape - Unfocus any active input widget
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && ImGui::IsAnyItemActive())
        {
            ImGui::SetKeyboardFocusHere(-1);
        }

        // Ctrl+L - Clear search query
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L))
        {
            ui.search.query[0] = '\0';
        }

        // n - Go to next match (only when no input widget is focused)
        if (ImGui::IsKeyPressed(ImGuiKey_N) && !io.KeyShift && !ImGui::IsAnyItemActive())
        {
            if (search.has_source() && !search.matches().empty())
            {
                const auto &matches = search.matches();
                const auto &line_index = search.line_index();

                // First match on a line strictly after the cursor line.
                // Using line (not byte offset) avoids getting stuck on a match
                // whose start is > line_start(cursor_line) on the same line.
                auto it = std::upper_bound(matches.begin(), matches.end(), ui.cursor.line,
                                           [&](size_t line, const Match &m)
                                           { return line < line_index.line_of(m.start); });

                if (it != matches.end())
                {
                    ui.cursor.line = line_index.line_of(it->start);
                }
                else
                {
                    ui.cursor.line = line_index.line_of(matches[0].start);
                }
            }
        }

        // Shift+N - Go to previous match (only when no input widget is focused)
        if (ImGui::IsKeyPressed(ImGuiKey_N) && io.KeyShift && !ImGui::IsAnyItemActive())
        {
            if (search.has_source() && !search.matches().empty())
            {
                const auto &matches = search.matches();
                auto cursor_offset = search.line_index().line_start(ui.cursor.line);

                // Find previous match before cursor
                auto it = std::lower_bound(matches.begin(), matches.end(), cursor_offset,
                                           [](const Match &m, size_t offset)
                                           { return m.start < offset; });

                if (it != matches.begin())
                {
                    // Found previous match
                    --it;
                    ui.cursor.line = search.line_index().line_of(it->start);
                }
                else if (!matches.empty())
                {
                    // Wrap to last match
                    ui.cursor.line = search.line_index().line_of(matches.back().start);
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

        spdlog::debug("kestrel start");

        SearchController search;
        UiInputs ui;

        auto load_path = [&](std::string_view p) -> bool
        {
            if (!is_valid_file_path(p))
            {
                spdlog::warn("reject path: {}", p);
                ui.file.loading_error = "Invalid file path";
                return false;
            }

            // Start async loading
            ui.file.loading = true;
            ui.file.loading_path = p;
            ui.file.loading_error.clear();
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
            if (!ui.search.case_sensitive)
                flags |= HS_FLAG_CASELESS;
            if (ui.search.dotall)
                flags |= HS_FLAG_DOTALL;
            if (ui.search.multiline)
                flags |= HS_FLAG_MULTILINE;

            search.set_pattern(ui.search.query, flags);
            search.tick(glfwGetTime());

            // Update loading state from SearchController
            bool was_loading = ui.file.loading;
            ui.file.loading = search.is_loading();

            if (was_loading && !ui.file.loading)
            {
                // Loading just finished
                std::string error = search.get_loading_error();
                if (!error.empty())
                {
                    ui.file.loading_error = error;
                }
                else if (search.has_source())
                {
                    // Successfully loaded - add to recent files
                    add_recent_file(ui, ui.file.loading_path);
                    ui.file.loading_error.clear();
                }
            }

            handle_cursor_input(ui, search);
            handle_keyboard_shortcuts(ui, search);

            w.begin_frame();
            draw_ui(ui, search);
            if (ui.file.pending_open)
            {
                load_path(*ui.file.pending_open);
                ui.file.pending_open.reset();
            }
            w.end_frame();
        }

        save_config(ui);
        spdlog::debug("kestrel exit");
        return 0;
    }

} // namespace kestrel
