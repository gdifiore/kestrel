#include "ui_internal.hpp"

#include "kestrel/line_index.hpp"
#include "kestrel/search.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

namespace kestrel
{

    void draw_minimap(UiInputs &in, const SearchController &search)
    {
        if (!in.view.show_minimap)
            return;
        ImGuiViewport *vp = ImGui::GetMainViewport();
        float window_pos_x = vp->WorkPos.x + vp->WorkSize.x - MINIMAP_WIDTH;
        float window_pos_y = vp->WorkPos.y + (in.layout.search_bar_h + in.layout.toolbar_h);
        int window_height_px = (int)(vp->WorkSize.y - (in.layout.search_bar_h + in.layout.toolbar_h));

        const std::vector<size_t> &orig_matches = search.matched_lines();
        const bool pattern_active = !search.pattern_empty();
        const bool filtered = in.view.display_only_filtered_lines && pattern_active;
        const bool use_custom_view = in.layout.view_has_custom;
        const std::vector<size_t> &view_lines = in.layout.view_lines;
        int line_count;
        if (use_custom_view)
            line_count = (int)view_lines.size();
        else if (filtered)
            line_count = (int)orig_matches.size();
        else
            line_count = (int)search.line_index().line_count();

        // cursor's row index in the displayed coordinate space (not source line).
        int cursor_row = source_to_display_row(in, orig_matches, filtered, in.cursor.line);

        ImGui::SetNextWindowPos(ImVec2(window_pos_x, window_pos_y));
        ImGui::SetNextWindowSize(ImVec2(MINIMAP_WIDTH, (float)window_height_px));

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##minimap", nullptr, flags))
        {
            ImDrawList *dl = ImGui::GetWindowDrawList();

            // Background fill for contrast.
            dl->AddRectFilled(
                ImVec2(window_pos_x, window_pos_y),
                ImVec2(window_pos_x + MINIMAP_WIDTH, window_pos_y + window_height_px),
                ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.15f)));

            ImVec2 btn_min = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##minimap_hit", ImVec2(MINIMAP_WIDTH, (float)window_height_px));
            if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f)
            {
                if (ImGuiWindow *w = ImGui::FindWindowByName("##results"))
                {
                    float line_h = ImGui::GetTextLineHeightWithSpacing();
                    ImGui::SetScrollY(w, w->Scroll.y - ImGui::GetIO().MouseWheel * line_h * 3.0f);
                }
            }

            if (line_count > 0)
            {
                // Target line in display-row space under the mouse.
                float mouse_y = ImGui::GetIO().MousePos.y - btn_min.y;
                int target = (int)((mouse_y / (float)window_height_px) * line_count);
                target = std::clamp(target, 0, line_count - 1);
                const size_t src_line_for_target = use_custom_view ? view_lines[target]
                                                   : filtered      ? orig_matches[target]
                                                                   : (size_t)target;

                if (ImGui::IsItemActive())
                {
                    in.layout.pending_scroll_line = target;
                    in.cursor.line = src_line_for_target;
                    in.cursor.offset = 0;
                    // Suppress autoscroll-to-cursor next frame; pending_scroll_line
                    // already positions the view, and autoscroll would just redo it.
                    in.layout.last_cursor_line = in.cursor.line;
                    in.layout.last_cursor_offset = in.cursor.offset;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("line %zu", src_line_for_target + 1);

                // Row size: cap small-file rows to 4px; large files scale to fit exactly.
                float row_size_f;
                if (line_count < window_height_px)
                {
                    const float MAX_ROW_PX = 4.0f;
                    row_size_f = std::min((float)window_height_px / (float)line_count, MAX_ROW_PX);
                }
                else
                {
                    row_size_f = (float)window_height_px / (float)line_count;
                }
                float mark_h = std::max(row_size_f, 1.0f);

                // Match marks — iterate matches only (O(m), not O(n)). Under
                // custom view, map each match's source line to its display row
                // via binary search; matches outside the view are skipped.
                ImU32 match_col = ImGui::GetColorU32(ImVec4(
                    in.view.color_match.x, in.view.color_match.y, in.view.color_match.z, 0.6f));
                for (size_t k = 0; k < orig_matches.size(); k++)
                {
                    int row;
                    if (use_custom_view)
                    {
                        auto it = std::lower_bound(view_lines.begin(), view_lines.end(), orig_matches[k]);
                        if (it == view_lines.end() || *it != orig_matches[k])
                            continue;
                        row = (int)(it - view_lines.begin());
                    }
                    else
                    {
                        row = filtered ? (int)k : (int)orig_matches[k];
                    }
                    if (row >= line_count)
                        break;
                    float y = window_pos_y + row * row_size_f;
                    dl->AddRectFilled(
                        ImVec2(window_pos_x, y),
                        ImVec2(window_pos_x + MINIMAP_WIDTH, y + mark_h),
                        match_col);
                }

                // Viewport indicator.
                if ((in.layout.visible_line_last - in.layout.visible_line_first) < line_count)
                {
                    const float MIN_VIEWPORT_H = 30.0f;
                    float vp_h = (in.layout.visible_line_last - in.layout.visible_line_first) * row_size_f;
                    vp_h = std::max(vp_h, MIN_VIEWPORT_H);
                    float y = window_pos_y + in.layout.visible_line_first * row_size_f;
                    float max_y = window_pos_y + window_height_px - vp_h;
                    y = std::min(y, max_y);
                    dl->AddRectFilled(
                        ImVec2(window_pos_x, y),
                        ImVec2(window_pos_x + MINIMAP_WIDTH, y + vp_h),
                        ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 0.25f)));
                }

                // Cursor line — drawn last so it sits on top.
                if (cursor_row >= 0 && cursor_row < line_count)
                {
                    float cy = window_pos_y + cursor_row * row_size_f;
                    dl->AddRectFilled(
                        ImVec2(window_pos_x, cy),
                        ImVec2(window_pos_x + MINIMAP_WIDTH, cy + mark_h),
                        ImGui::GetColorU32(in.view.color_scope));
                }
            }
        }
        ImGui::End();
    }

} // namespace kestrel
