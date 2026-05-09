#include "kestrel/app.hpp"
#include "kestrel/config.hpp"
#include "kestrel/group_matcher.hpp"
#include "kestrel/search.hpp"
#include "kestrel/ui.hpp"
#include "kestrel/util.hpp"
#include "kestrel/window.hpp"

#include <GLFW/glfw3.h>
#include <hs.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace kestrel
{

    static void handle_cursor_input(UiInputs &ui, const SearchController &search)
    {
        if (!search.has_source())
            return;

        const auto &lines = search.line_index();
        const size_t total_lines = lines.line_count();
        if (total_lines == 0)
            return;

        auto &cur = ui.cursor;
        const ImGuiIO &io = ImGui::GetIO();

        bool nav_pressed =
            ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
            ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
            ImGui::IsKeyPressed(ImGuiKey_Home) ||
            ImGui::IsKeyPressed(ImGuiKey_End) ||
            ImGui::IsKeyPressed(ImGuiKey_PageUp) ||
            ImGui::IsKeyPressed(ImGuiKey_PageDown);

        if (nav_pressed)
            ui.selection.extend_or_clear(io.KeyShift, cur.line);

        // Step in display-row space, not source-line space. In filter / custom
        // view, source-line stepping lands the cursor on hidden lines: the
        // counts shift but the view stays put.
        const ViewIndex view = make_view_index(ui, search);
        const int row_count = view.row_count();

        if (row_count > 0)
        {
            const int max_row = row_count - 1;
            int cur_row = std::clamp(view.source_to_row(cur.line), 0, max_row);

            // Page step = real results-pane height in rows, minus 1 row for
            // overlap context. Subtract chrome (search bar + toolbar + status
            // bar) from main viewport so PgDn doesn't overshoot the viewport.
            float line_height = ImGui::GetTextLineHeightWithSpacing();
            float vp_h = ImGui::GetMainViewport()->WorkSize.y;
            float chrome = ui.layout.search_bar_h + ui.layout.toolbar_h + ui.layout.status_bar_h;
            float results_h = std::max(line_height, vp_h - chrome);
            int page_step = std::max(1, static_cast<int>(results_h / line_height) - 1);

            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                cur_row = std::max(cur_row - 1, 0);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
                cur_row = std::min(cur_row + 1, max_row);
            if (ImGui::IsKeyPressed(ImGuiKey_Home))
                cur_row = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_End))
                cur_row = max_row;
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
                cur_row = std::max(cur_row - page_step, 0);
            if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
                cur_row = std::min(cur_row + page_step, max_row);

            cur.line = view.row_to_source(std::clamp(cur_row, 0, max_row));
        }

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

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && !ImGui::IsAnyItemActive())
        {
            if (ui.selection.anchor_line && search.has_source())
            {
                const auto &lines = search.line_index();
                const auto bytes = search.source_bytes();
                const size_t total = lines.line_count();
                size_t a = *ui.selection.anchor_line;
                size_t b = ui.cursor.line;
                if (a > b) std::swap(a, b);
                if (b >= total) b = total - 1;

                std::string out;
                size_t lo = lines.line_start(a);
                size_t hi = (b + 1 < total) ? lines.line_start(b + 1) : bytes.size();
                if (hi > lo)
                {
                    out.assign(bytes.data() + lo, bytes.data() + hi);
                    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
                        out.pop_back();
                }
                ImGui::SetClipboardText(out.c_str());
            }
            else
            {
                ImGui::SetClipboardText(ui.search.query);
            }
        }

        // Ctrl+V - Paste to search pattern (when no input widget is focused)
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !ImGui::IsAnyItemActive())
        {
            const char *clipboard = ImGui::GetClipboardText();
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
                ui.file_load.loading_error = "Invalid file path";
                return false;
            }

            // Start async loading
            ui.file_load.loading = true;
            ui.file_load.loading_path = p;
            ui.file_load.loading_error.clear();
            search.load_source_async(p);
            return true;
        };

        if (args->file_path && !load_path(*args->file_path))
            return 1;

        Window w("kestrel", 800, 600);
        w.on_file_drop([&](std::span<const char *> paths)
                       {
        if (!paths.empty()) load_path(paths[0]); });
        // Repaint synchronously while the user drags the window edge — without
        // this, X11/Wayland blocks event polling until the drag ends and the
        // window contents lag/freeze behind the new frame size.
        w.on_refresh([&]() {
            w.begin_frame();
            draw_ui(ui, search);
            w.end_frame();
        });
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

            if (ui.view.highlight_groups)
            {
                // Recompile secondary (group) matcher on pattern/flag change.
                // PCRE2 extracts capture-group spans from matches Hyperscan already
                // flagged; compile failure is silent (falls back to plain match rects).
                static std::string gm_last_pattern;
                static unsigned gm_last_flags = ~0u;
                if (ui.search.query != gm_last_pattern || flags != gm_last_flags)
                {
                    gm_last_pattern = ui.search.query;
                    gm_last_flags = flags;
                    if (gm_last_pattern.empty())
                        ui.groupmatch.group_matcher_.reset();
                    else
                        ui.groupmatch.group_matcher_ = GroupMatcher::compile(gm_last_pattern, flags);
                }
            }

            search.tick(glfwGetTime());

            // Update loading state from SearchController
            bool was_loading = ui.file_load.loading;
            ui.file_load.loading = search.is_loading();

            if (was_loading && !ui.file_load.loading)
            {
                // Loading just finished
                std::string error = search.get_loading_error();
                if (!error.empty())
                {
                    ui.file_load.loading_error = error;
                }
                else if (search.has_source())
                {
                    // Successfully loaded - add to recent files
                    add_recent_file(ui, ui.file_load.loading_path);
                    ui.file_load.loading_error.clear();
                    ui.file_load.current_path = ui.file_load.loading_path;
                    std::string fname = std::filesystem::path(ui.file_load.current_path).filename().string();
                    w.set_title("kestrel \xE2\x80\x94 " + fname);
                }
            }

            handle_cursor_input(ui, search);
            handle_keyboard_shortcuts(ui, search);

            w.begin_frame();
            draw_ui(ui, search);
            if (ui.file_load.pending_open)
            {
                load_path(*ui.file_load.pending_open);
                ui.file_load.pending_open.reset();
            }
            w.end_frame();
        }

        save_config(ui);
        spdlog::debug("kestrel exit");
        return 0;
    }

} // namespace kestrel
