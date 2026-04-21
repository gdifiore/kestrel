#include "kestrel/ui.hpp"

#include "kestrel/config.hpp"
#include "kestrel/line_index.hpp"
#include "kestrel/search.hpp"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <span>

#include <imgui.h>
#include <ImGuiFileDialog.h>

namespace kestrel
{

    static void draw_main_menu(UiInputs &in)
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open...", "Ctrl+O"))
                {
                    IGFD::FileDialogConfig cfg;
                    cfg.path = ".";
                    ImGuiFileDialog::Instance()->OpenDialog(
                        "kestrel_open", "Open file", ".*,.txt,.log,.md", cfg);
                }

                // Recent files submenu
                if (!in.file.recent_files.empty() && ImGui::BeginMenu("Recent Files"))
                {
                    // Clean up non-existent files first
                    cleanup_recent_files(in);

                    for (size_t i = 0; i < in.file.recent_files.size(); ++i)
                    {
                        const std::string &path = in.file.recent_files[i];

                        // Show just filename, full path in tooltip
                        std::filesystem::path file_path(path);
                        std::string display_name = file_path.filename().string();

                        if (ImGui::MenuItem(display_name.c_str()))
                        {
                            in.file.pending_open = path;
                        }

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("%s", path.c_str());
                        }
                    }

                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear Recent"))
                    {
                        in.file.recent_files.clear();
                    }

                    ImGui::EndMenu();
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Ctrl+Q"))
                {
                    in.quit_requested = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                float &scale = ImGui::GetIO().FontGlobalScale;
                if (ImGui::MenuItem("Zoom In", "Ctrl+="))
                    scale = std::min(scale + 0.1f, 3.0f);
                if (ImGui::MenuItem("Zoom Out", "Ctrl+-"))
                    scale = std::max(scale - 0.1f, 0.5f);
                if (ImGui::MenuItem("Reset Zoom", "Ctrl+0"))
                    scale = 1.0f;
                if (ImGui::MenuItem("Dark Mode", nullptr, &in.view.is_dark_mode))
                {
                    if (in.view.is_dark_mode)
                        ImGui::StyleColorsDark();
                    else
                        ImGui::StyleColorsLight();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Handle keyboard shortcuts for file dialog
        if (in.hotkeys.trigger_open_dialog)
        {
            IGFD::FileDialogConfig cfg;
            cfg.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog(
                "kestrel_open", "Open file", ".*,.txt,.log,.md", cfg);
            in.hotkeys.trigger_open_dialog = false;
        }
    }

    static void draw_query_input(UiInputs &in)
    {
        // Handle Ctrl+F focus
        if (in.hotkeys.focus_search)
        {
            ImGui::SetKeyboardFocusHere();
            in.hotkeys.focus_search = false;
        }

        // ImGui InputText reverts buffer on Escape. Snapshot before the call
        // and restore if Escape caused the deactivation, so Esc only unfocuses.
        const bool esc_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
        char query_backup[IM_ARRAYSIZE(in.search.query)];
        if (esc_pressed)
            std::memcpy(query_backup, in.search.query, sizeof(in.search.query));

        ImGui::InputTextWithHint("##query", "search...", in.search.query, IM_ARRAYSIZE(in.search.query));

        if (esc_pressed && ImGui::IsItemDeactivated())
            std::memcpy(in.search.query, query_backup, sizeof(in.search.query));
    }

    static void draw_toolbar(UiInputs &in, const SearchController &search)
    {
        ImGui::SameLine();
        if (ImGui::Button(" * "))
            in.show_settings = !in.show_settings;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Settings");

        ImGui::Checkbox("Aa", &in.search.case_sensitive);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Case sensitive");

        ImGui::SameLine();
        ImGui::Checkbox(".*", &in.search.dotall);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Dot matches newlines\n(. matches \\n and all characters)");

        ImGui::SameLine();
        ImGui::Checkbox("^$", &in.search.multiline);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Multiline anchors\n(^ and $ match line boundaries)");

        ImGui::SameLine();
        ImGui::Text("%d before", in.layout.matches_before);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Matches before cursor");

        ImGui::SameLine();
        ImGui::Text("%d after", in.layout.matches_after);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Matches after cursor");

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("%zu matches", search.matches().size());
        ImGui::SameLine();
        ImGui::TextDisabled("%.2f ms", search.last_scan_ms());

        ImGui::SameLine();
        ImGui::ColorEdit4("match", &in.view.color_match.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        ImGui::SameLine();
        ImGui::TextUnformatted("match");

        ImGui::SameLine();
        ImGui::ColorEdit4("cursor", &in.view.color_scope.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        ImGui::SameLine();
        ImGui::TextUnformatted("cursor");

        ImGui::SameLine();
        ImGui::Checkbox("line #", &in.view.show_line_nums);

        if (!search.compile_error().empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", search.compile_error().c_str());
        }
    }

    static void draw_search_bar(UiInputs &in, const SearchController &search)
    {
        ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 0));

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##search_bar", nullptr, flags))
        {
            const float gear_w = ImGui::CalcTextSize(" * ").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - gear_w - ImGui::GetStyle().ItemSpacing.x);

            draw_query_input(in);
            draw_toolbar(in, search);

            // Measure after all widgets are placed so the results window below
            // sits flush, even when the error row appears or disappears.
            in.layout.search_bar_h = ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y;
        }
        ImGui::End();
    }

    static void draw_loading_spinner(const std::string &loading_path, const std::string &loading_error)
    {
        ImGui::Text("Loading file: %s", loading_path.c_str());

        static float spinner_angle = 0.0f;
        spinner_angle += 0.1f;
        if (spinner_angle >= 2.0f * 3.14159f)
            spinner_angle = 0.0f;

        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 center = ImVec2(pos.x + 20, pos.y + 10);
        float radius = 8.0f;
        ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

        for (int i = 0; i < 12; i++)
        {
            float angle = spinner_angle + (i * 2.0f * 3.14159f / 12.0f);
            float alpha = (12 - i) / 12.0f;
            ImU32 fade_color = (color & 0x00FFFFFF) | (IM_COL32_A_MASK & ImU32(alpha * 255) << IM_COL32_A_SHIFT);
            ImVec2 dot_pos = ImVec2(center.x + radius * cosf(angle), center.y + radius * sinf(angle));
            draw_list->AddCircleFilled(dot_pos, 2.0f, fade_color);
        }

        ImGui::Dummy(ImVec2(40, 20)); // Reserve space for spinner

        if (!loading_error.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", loading_error.c_str());
        }
    }

    static void draw_line_row(UiInputs &in, const SearchController &search,
                              const LineIndex &lines, int total_lines,
                              std::span<const char> source_bytes, int line_idx,
                              float line_height, float char_width)
    {
        if (in.view.show_line_nums)
        {
            if (in.cursor.visible && line_idx == static_cast<int>(in.cursor.line))
            {
                // Highlight cursor line number with cursor color
                ImGui::TextColored(in.view.color_scope, "%7d ", line_idx + 1);
            }
            else
            {
                ImGui::TextDisabled("%7d ", line_idx + 1);
            }
            ImGui::SameLine();
        }
        std::size_t start = lines.line_start(line_idx);
        std::size_t end = (line_idx + 1 < total_lines)
                              ? lines.line_start(line_idx + 1)
                              : source_bytes.size();
        while (end > start && (source_bytes[end - 1] == '\n' || source_bytes[end - 1] == '\r'))
            --end;
        const char *p = source_bytes.data() + start;

        ImVec2 cursor_pos = ImGui::GetCursorScreenPos();

        // Cursor line highlighting
        if (in.cursor.visible && line_idx == static_cast<int>(in.cursor.line))
        {
            ImVec2 line_min(cursor_pos.x, cursor_pos.y);
            ImVec2 line_max(cursor_pos.x + ImGui::GetContentRegionAvail().x, cursor_pos.y + line_height);
            ImVec4 cursor_color_with_alpha = in.view.color_scope;
            cursor_color_with_alpha.w = 0.3f; // Semi-transparent background
            ImU32 cursor_color = ImGui::GetColorU32(cursor_color_with_alpha);
            ImGui::GetWindowDrawList()->AddRectFilled(line_min, line_max, cursor_color);
        }

        std::span matches_in_range = search.matches_in_range(start, end);

        const GroupMatcher *gm = nullptr;
        bool has_groups = false;

        thread_local std::vector<GroupMatcher::Span> group_spans;
        thread_local std::vector<int> group_indices;

        const auto &group_palette = in.view.group_colors;
        int palette_size = 0;

        if (in.view.highlight_groups)
        {
            palette_size = static_cast<int>(group_palette.size());
            gm = in.groupmatch.group_matcher_ ? &*in.groupmatch.group_matcher_ : nullptr;
            has_groups = gm && gm->group_count() > 0;
        }

        // TODO: col_start/col_end are byte offsets. Correct for ASCII;
        // breaks for multibyte UTF-8 (one codepoint spans multiple bytes).
        // Revisit when we care about non-ASCII logs.
        for (auto match : matches_in_range)
        {
            std::size_t col_start = match.start - start;
            std::size_t col_end = std::min(match.end, end) - start;

            ImVec2 p_min(cursor_pos.x + col_start * char_width, cursor_pos.y);
            ImVec2 p_max(cursor_pos.x + col_end * char_width, cursor_pos.y + line_height);
            ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, ImGui::GetColorU32(in.view.color_match));

            if (in.view.highlight_groups && has_groups && match.end <= end)
            {
                group_spans.clear();
                group_indices.clear();
                gm->match_into(source_bytes, match.start, match.end, group_spans, group_indices);
                for (std::size_t i = 0; i < group_spans.size(); ++i)
                {
                    const auto &g = group_spans[i];
                    if (g.end <= g.start)
                        continue; // zero-width: skip
                    std::size_t g_col_start = g.start - start;
                    std::size_t g_col_end = g.end - start;
                    ImVec2 g_min(cursor_pos.x + g_col_start * char_width, cursor_pos.y);
                    ImVec2 g_max(cursor_pos.x + g_col_end * char_width, cursor_pos.y + line_height);
                    ImVec4 col = group_palette[(group_indices[i] - 1) % palette_size];
                    col.w = 0.75f;
                    ImGui::GetWindowDrawList()->AddRectFilled(g_min, g_max, ImGui::GetColorU32(col));
                }
            }
        }

        ImGui::TextUnformatted(p, p + (end - start));

        // Mouse click to position cursor
        if (ImGui::IsItemClicked())
        {
            in.cursor.line = static_cast<size_t>(line_idx);
        }
    }

    static void autoscroll_to_cursor(const UiInputs &in, const std::vector<size_t> &matched,
                                     bool filter_view, int view_count,
                                     const ImGuiListClipper &clipper)
    {
        const int cursor_line = static_cast<int>(in.cursor.line);

        int cursor_view_pos = cursor_line;
        if (filter_view)
        {
            cursor_view_pos = -1;
            for (int i = 0; i < view_count; ++i)
            {
                if (static_cast<int>(matched[i]) == cursor_line)
                {
                    cursor_view_pos = i;
                    break;
                }
            }
        }

        if (cursor_view_pos >= 0 &&
            (cursor_view_pos < clipper.DisplayStart ||
             cursor_view_pos >= clipper.DisplayEnd))
        {
            float scroll_line_height = ImGui::GetTextLineHeightWithSpacing();
            float target_scroll = cursor_view_pos * scroll_line_height - (ImGui::GetWindowHeight() * 0.5f);
            target_scroll = std::max(0.0f, target_scroll);
            ImGui::SetScrollY(target_scroll);
        }
    }

    static void draw_results(UiInputs &in, const SearchController &search)
    {
        ImGuiViewport *vp = ImGui::GetMainViewport();
        float window_pos_y = vp->WorkPos.y + in.layout.search_bar_h;
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, window_pos_y));
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - in.layout.search_bar_h));

        // NoDecoration would strip the scrollbar too — spell out the flags we want.
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##results", nullptr, flags))
        {
            // Snap scroll to whole lines — prevents half-clipped rows when the
            // trackpad lands on a sub-line offset. Skip near max scroll: flooring
            // there would drop the scroll below max and clip the last line.
            if (in.view.snap_scroll)
            {
                float line_h = ImGui::GetTextLineHeightWithSpacing();
                float scroll_y = ImGui::GetScrollY();
                float max_y = ImGui::GetScrollMaxY();
                float snapped = std::floor(scroll_y / line_h) * line_h;
                if (snapped != scroll_y && scroll_y < max_y - 0.5f)
                    ImGui::SetScrollY(snapped);
            }

            const bool has_source = search.has_source();
            auto source_bytes = has_source ? search.source_bytes() : std::span<const char>{};

            if (in.file.loading)
            {
                draw_loading_spinner(in.file.loading_path, in.file.loading_error);
            }
            else if (!has_source || source_bytes.empty())
            {
                if (!in.file.loading_error.empty())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load file: %s", in.file.loading_error.c_str());
                }
                else
                {
                    ImGui::TextDisabled("No file loaded. Drop a file or use File > Open...");
                }
            }
            else
            {
                const LineIndex &lines = search.line_index();
                const int total_lines = static_cast<int>(lines.line_count());
                const bool filtered = !search.pattern_empty();
                const auto &matched = search.matched_lines();
                const bool filter_view = in.view.display_only_filtered_lines && filtered;
                const int view_count = filter_view ? static_cast<int>(matched.size()) : total_lines;
                // Average over many glyphs: per-glyph rounding in CalcTextSize
                // accumulates otherwise, and match rects drift off by ~1 char per ~40.
                float char_width = ImGui::CalcTextSize(
                                       "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM")
                                       .x /
                                   50.0f;
                float line_height = ImGui::GetTextLineHeight();

                if (filtered && matched.empty() && search.compile_error().empty())
                {
                    ImGui::TextDisabled("No matches.");
                }

                ImGuiListClipper clipper;
                clipper.Begin(view_count);
                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    {
                        const int line_idx = filter_view ? static_cast<int>(matched[i]) : i;
                        draw_line_row(in, search, lines, total_lines, source_bytes,
                                      line_idx, line_height, char_width);
                    }
                }

                if (in.cursor.visible && has_source)
                {
                    autoscroll_to_cursor(in, matched, filter_view, view_count, clipper);
                }
            }
        }
        ImGui::End();
    }

    static void draw_shortcut_row(const char *key, const char *action)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "%s", key);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(action);
    }

    static void draw_shortcuts_table()
    {
        if (!ImGui::BeginTable("shortcuts", 2, ImGuiTableFlags_SizingFixedFit))
            return;
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);

        draw_shortcut_row("Ctrl+F", "Focus search box");
        draw_shortcut_row("Ctrl+O", "Open file dialog");
        draw_shortcut_row("Ctrl+Q", "Quit application");
        draw_shortcut_row("Escape", "Unfocus input");
        draw_shortcut_row("Ctrl+L", "Clear search");
        draw_shortcut_row("n", "Next match");
        draw_shortcut_row("Shift+N", "Previous match");
        draw_shortcut_row("↑↓ PgUp/Dn", "Navigate lines");
        draw_shortcut_row("Home/End", "First/last line");
        draw_shortcut_row("Ctrl+G", "Go to line");
        draw_shortcut_row("Ctrl+C", "Copy search pattern");
        draw_shortcut_row("Ctrl+V", "Paste to search");

        ImGui::EndTable();
    }

    static void draw_settings_popup(UiInputs &in)
    {
        if (!in.show_settings)
            return;
        if (ImGui::Begin("Settings", &in.show_settings, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::SeparatorText("Display");
            ImGui::Checkbox("Snap scroll to lines", &in.view.snap_scroll);
            ImGui::Checkbox("Show only filtered results", &in.view.display_only_filtered_lines);
            ImGui::Checkbox("Color each regex capture group individually", &in.view.highlight_groups);

            ImGui::SeparatorText("Regex Flags");
            ImGui::Checkbox("Case sensitive", &in.search.case_sensitive);

            ImGui::Checkbox("Dot matches newlines", &in.search.dotall);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Make . (dot) match newline characters\nPattern 'foo.*bar' can match across lines");

            ImGui::Checkbox("Multiline anchors", &in.search.multiline);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Make ^ and $ match line boundaries\n^ = start of line, $ = end of line");

            ImGui::SeparatorText("Keyboard Shortcuts");
            draw_shortcuts_table();
        }
        ImGui::End();
    }

    static void draw_open_dialog(UiInputs &in)
    {
        auto *dlg = ImGuiFileDialog::Instance();
        ImVec2 min_size(600, 400);
        if (dlg->Display("kestrel_open", ImGuiWindowFlags_NoCollapse, min_size))
        {
            if (dlg->IsOk())
            {
                in.file.pending_open = dlg->GetFilePathName();
            }
            dlg->Close();
        }
    }

    static void draw_goto_line_dialog(UiInputs &in, const SearchController &search)
    {
        if (!in.hotkeys.show_goto_line)
            return;

        if (ImGui::Begin("Go to Line", &in.hotkeys.show_goto_line, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
        {
            ImGui::Text("Enter line number:");
            ImGui::SetNextItemWidth(200.0f);

            bool enter_pressed = ImGui::InputText("##line", in.hotkeys.goto_line_input, sizeof(in.hotkeys.goto_line_input), ImGuiInputTextFlags_EnterReturnsTrue);

            if (ImGui::IsWindowAppearing())
            {
                ImGui::SetKeyboardFocusHere(-1); // Focus the input field
            }

            ImGui::Spacing();

            bool go_button = ImGui::Button("Go");
            ImGui::SameLine();
            bool cancel_button = ImGui::Button("Cancel");

            if (enter_pressed || go_button)
            {
                // Parse line number and jump to it
                char *endptr;
                long line_num = strtol(in.hotkeys.goto_line_input, &endptr, 10);

                if (*endptr == '\0' && line_num > 0 && search.has_source())
                {
                    // Convert to 0-based and clamp to valid range
                    size_t target_line = static_cast<size_t>(line_num - 1);
                    size_t max_line = search.line_index().line_count();
                    if (max_line > 0)
                    {
                        target_line = std::min(target_line, max_line - 1);
                        in.cursor.line = target_line;
                        in.cursor.offset = search.line_index().line_start(target_line);
                    }
                }

                // Close dialog and clear input
                in.hotkeys.show_goto_line = false;
                in.hotkeys.goto_line_input[0] = '\0';
            }

            if (cancel_button)
            {
                in.hotkeys.show_goto_line = false;
                in.hotkeys.goto_line_input[0] = '\0';
            }
        }
        ImGui::End();
    }

    void draw_ui(UiInputs &in, const SearchController &search)
    {
        draw_main_menu(in);
        draw_search_bar(in, search);
        draw_results(in, search);
        draw_settings_popup(in);
        draw_open_dialog(in);
        draw_goto_line_dialog(in, search);
    }

} // namespace kestrel
