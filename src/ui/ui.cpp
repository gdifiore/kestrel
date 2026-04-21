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
                if (!in.recent_files.empty() && ImGui::BeginMenu("Recent Files"))
                {
                    // Clean up non-existent files first
                    cleanup_recent_files(in);

                    for (size_t i = 0; i < in.recent_files.size(); ++i)
                    {
                        const std::string &path = in.recent_files[i];

                        // Show just filename, full path in tooltip
                        std::filesystem::path file_path(path);
                        std::string display_name = file_path.filename().string();

                        if (ImGui::MenuItem(display_name.c_str()))
                        {
                            in.pending_open = path;
                        }

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("%s", path.c_str());
                        }
                    }

                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear Recent"))
                    {
                        in.recent_files.clear();
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
                if (ImGui::MenuItem("Dark Mode", nullptr, &in.is_dark_mode))
                {
                    if (in.is_dark_mode)
                        ImGui::StyleColorsDark();
                    else
                        ImGui::StyleColorsLight();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Handle keyboard shortcuts for file dialog
        if (in.trigger_open_dialog)
        {
            IGFD::FileDialogConfig cfg;
            cfg.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog(
                "kestrel_open", "Open file", ".*,.txt,.log,.md", cfg);
            in.trigger_open_dialog = false;
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

            // Handle Ctrl+F focus
            if (in.focus_search)
            {
                ImGui::SetKeyboardFocusHere();
                in.focus_search = false;
            }

            ImGui::InputTextWithHint("##query", "search...", in.query, IM_ARRAYSIZE(in.query));
            ImGui::SameLine();
            if (ImGui::Button(" * "))
                in.show_settings = !in.show_settings;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Settings");

            ImGui::Checkbox("Aa", &in.case_sensitive);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Case sensitive");

            ImGui::SameLine();
            ImGui::Checkbox(".*", &in.dotall);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Dot matches newlines\n(. matches \\n and all characters)");

            ImGui::SameLine();
            ImGui::Checkbox("^$", &in.multiline);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Multiline anchors\n(^ and $ match line boundaries)");

            ImGui::SameLine();
            ImGui::Text("%d before", in.matches_before);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Matches before cursor");

            ImGui::SameLine();
            ImGui::Text("%d after", in.matches_after);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Matches after cursor");

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::Text("%zu matches", search.matches().size());
            ImGui::SameLine();
            ImGui::TextDisabled("%.2f ms", search.last_scan_ms());

            ImGui::SameLine();
            ImGui::ColorEdit4("match", &in.color_match.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            ImGui::SameLine();
            ImGui::TextUnformatted("match");

            ImGui::SameLine();
            ImGui::ColorEdit4("cursor", &in.color_scope.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            ImGui::SameLine();
            ImGui::TextUnformatted("cursor");

            ImGui::SameLine();
            ImGui::Checkbox("line #", &in.show_line_nums);

            if (!search.compile_error().empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", search.compile_error().c_str());
            }
            // Measure after all widgets are placed so the results window below
            // sits flush, even when the error row appears or disappears.
            in.search_bar_h = ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y;
        }
        ImGui::End();
    }

    static void draw_results(UiInputs &in, const SearchController &search)
    {
        ImGuiViewport *vp = ImGui::GetMainViewport();
        float window_pos_y = vp->WorkPos.y + in.search_bar_h;
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, window_pos_y));
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - in.search_bar_h));

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
            if (in.snap_scroll)
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

            if (in.loading)
            {
                // Show loading indicator
                ImGui::Text("Loading file: %s", in.loading_path.c_str());

                // Animated spinner
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

                if (!in.loading_error.empty())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", in.loading_error.c_str());
                }
            }
            else if (!has_source || source_bytes.empty())
            {
                if (!in.loading_error.empty())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load file: %s", in.loading_error.c_str());
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
                const int view_count = filtered ? static_cast<int>(matched.size()) : total_lines;
                // Average over many glyphs: per-glyph rounding in CalcTextSize
                // accumulates otherwise, and match rects drift off by ~1 char per ~40.
                float char_width = ImGui::CalcTextSize(
                                       "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM")
                                       .x /
                                   50.0f;
                float line_height = ImGui::GetTextLineHeight();

                if (filtered && view_count == 0 && search.compile_error().empty())
                {
                    ImGui::TextDisabled("No matches.");
                }

                ImGuiListClipper clipper;
                clipper.Begin(view_count);
                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    {
                        const int line_idx = in.display_only_filtered_lines
                                                 ? (filtered ? static_cast<int>(matched[i]) : i)
                                                 : i;

                        if (in.show_line_nums)
                        {
                            if (in.cursor_visible && line_idx == static_cast<int>(in.cursor_line))
                            {
                                // Highlight cursor line number with cursor color
                                ImGui::TextColored(in.color_scope, "%7d ", line_idx + 1);
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
                        if (in.cursor_visible && line_idx == static_cast<int>(in.cursor_line))
                        {
                            ImVec2 line_min(cursor_pos.x, cursor_pos.y);
                            ImVec2 line_max(cursor_pos.x + ImGui::GetContentRegionAvail().x, cursor_pos.y + line_height);
                            ImVec4 cursor_color_with_alpha = in.color_scope;
                            cursor_color_with_alpha.w = 0.3f; // Semi-transparent background
                            ImU32 cursor_color = ImGui::GetColorU32(cursor_color_with_alpha);
                            ImGui::GetWindowDrawList()->AddRectFilled(line_min, line_max, cursor_color);
                        }

                        std::span matches_in_range = search.matches_in_range(start, end);

                        // TODO: col_start/col_end are byte offsets. Correct for ASCII;
                        // breaks for multibyte UTF-8 (one codepoint spans multiple bytes).
                        // Revisit when we care about non-ASCII logs.
                        for (auto match : matches_in_range)
                        {
                            std::size_t col_start = match.start - start;
                            std::size_t col_end = std::min(match.end, end) - start;

                            ImVec2 p_min(cursor_pos.x + col_start * char_width, cursor_pos.y);
                            ImVec2 p_max(cursor_pos.x + col_end * char_width, cursor_pos.y + line_height);
                            ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, ImGui::GetColorU32(in.color_match));
                        }

                        ImGui::TextUnformatted(p, p + (end - start));

                        // Mouse click to position cursor
                        if (ImGui::IsItemClicked())
                        {
                            in.cursor_line = static_cast<size_t>(line_idx);
                        }
                    }
                }

                // Auto-scroll to keep cursor visible
                if (in.cursor_visible && has_source)
                {
                    const int cursor_line = static_cast<int>(in.cursor_line);

                    // Find cursor position in current view
                    int cursor_view_pos = -1;
                    if (in.display_only_filtered_lines && filtered)
                    {
                        // Filter mode: Find cursor line in matched_lines array
                        for (int i = 0; i < view_count; ++i)
                        {
                            if (static_cast<int>(matched[i]) == cursor_line)
                            {
                                cursor_view_pos = i;
                                break;
                            }
                        }
                    }
                    else
                    {
                        // Highlight mode or no filter: view position = line number
                        cursor_view_pos = cursor_line;
                    }

                    // Scroll to cursor if not visible or not found in filtered view
                    if (cursor_view_pos == -1 ||
                        cursor_view_pos < clipper.DisplayStart ||
                        cursor_view_pos >= clipper.DisplayEnd)
                    {

                        if (cursor_view_pos >= 0)
                        {
                            float scroll_line_height = ImGui::GetTextLineHeightWithSpacing();
                            float target_scroll = cursor_view_pos * scroll_line_height - (ImGui::GetWindowHeight() * 0.5f);
                            target_scroll = std::max(0.0f, target_scroll);
                            ImGui::SetScrollY(target_scroll);
                        }
                    }
                }
            }
        }
        ImGui::End();
    }

    static void draw_settings_popup(UiInputs &in)
    {
        if (!in.show_settings)
            return;
        if (ImGui::Begin("Settings", &in.show_settings, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::SeparatorText("Display");
            ImGui::Checkbox("Snap scroll to lines", &in.snap_scroll);
            ImGui::Checkbox("Show only filtered results", &in.display_only_filtered_lines);

            ImGui::SeparatorText("Regex Flags");
            ImGui::Checkbox("Case sensitive", &in.case_sensitive);

            ImGui::Checkbox("Dot matches newlines", &in.dotall);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Make . (dot) match newline characters\nPattern 'foo.*bar' can match across lines");

            ImGui::Checkbox("Multiline anchors", &in.multiline);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Make ^ and $ match line boundaries\n^ = start of line, $ = end of line");

            ImGui::SeparatorText("Keyboard Shortcuts");

            // Create two-column layout for shortcuts
            if (ImGui::BeginTable("shortcuts", 2, ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Ctrl+F");
                ImGui::TableNextColumn();
                ImGui::Text("Focus search box");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Ctrl+O");
                ImGui::TableNextColumn();
                ImGui::Text("Open file dialog");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Ctrl+Q");
                ImGui::TableNextColumn();
                ImGui::Text("Quit application");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Escape");
                ImGui::TableNextColumn();
                ImGui::Text("Clear search");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "n");
                ImGui::TableNextColumn();
                ImGui::Text("Next match");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Shift+N");
                ImGui::TableNextColumn();
                ImGui::Text("Previous match");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "↑↓ PgUp/Dn");
                ImGui::TableNextColumn();
                ImGui::Text("Navigate lines");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Home/End");
                ImGui::TableNextColumn();
                ImGui::Text("First/last line");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Ctrl+G");
                ImGui::TableNextColumn();
                ImGui::Text("Go to line");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Ctrl+C");
                ImGui::TableNextColumn();
                ImGui::Text("Copy search pattern");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Ctrl+V");
                ImGui::TableNextColumn();
                ImGui::Text("Paste to search");

                ImGui::EndTable();
            }
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
                in.pending_open = dlg->GetFilePathName();
            }
            dlg->Close();
        }
    }

    static void draw_goto_line_dialog(UiInputs &in, const SearchController &search)
    {
        if (!in.show_goto_line)
            return;

        if (ImGui::Begin("Go to Line", &in.show_goto_line, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
        {
            ImGui::Text("Enter line number:");
            ImGui::SetNextItemWidth(200.0f);

            bool enter_pressed = ImGui::InputText("##line", in.goto_line_input, sizeof(in.goto_line_input), ImGuiInputTextFlags_EnterReturnsTrue);

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
                long line_num = strtol(in.goto_line_input, &endptr, 10);

                if (*endptr == '\0' && line_num > 0 && search.has_source())
                {
                    // Convert to 0-based and clamp to valid range
                    size_t target_line = static_cast<size_t>(line_num - 1);
                    size_t max_line = search.line_index().line_count();
                    if (max_line > 0)
                    {
                        target_line = std::min(target_line, max_line - 1);
                        in.cursor_line = target_line;
                        in.cursor_offset = search.line_index().line_start(target_line);
                    }
                }

                // Close dialog and clear input
                in.show_goto_line = false;
                in.goto_line_input[0] = '\0';
            }

            if (cancel_button)
            {
                in.show_goto_line = false;
                in.goto_line_input[0] = '\0';
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
