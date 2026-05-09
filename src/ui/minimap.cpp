#include "ui_internal.hpp"

#include "kestrel/line_index.hpp"
#include "kestrel/search.hpp"

#include <algorithm>
#include <cstddef>

#include <imgui.h>
#include <imgui_internal.h>

namespace kestrel
{
    namespace
    {
        struct MinimapGeometry
        {
            float pos_x;
            float pos_y;
            int height_px;
            float width_px;
        };

        MinimapGeometry compute_geometry(const UiInputs &in)
        {
            ImGuiViewport *vp = ImGui::GetMainViewport();
            const float top_h = in.layout.search_bar_h + in.layout.toolbar_h;
            const float w = minimap_width();
            return {
                vp->WorkPos.x + vp->WorkSize.x - w,
                vp->WorkPos.y + top_h,
                static_cast<int>(vp->WorkSize.y - top_h - in.layout.status_bar_h),
                w,
            };
        }

        void handle_wheel_scroll()
        {
            if (!ImGui::IsItemHovered() || ImGui::GetIO().MouseWheel == 0.0F)
            {
                return;
            }
            ImGuiWindow *w = ImGui::FindWindowByName("##results");
            if (w == nullptr)
            {
                return;
            }
            float line_h = ImGui::GetTextLineHeightWithSpacing();
            ImGui::SetScrollY(w, w->Scroll.y - ImGui::GetIO().MouseWheel * line_h * 3.0F);
        }

        void handle_drag_and_tooltip(UiInputs &in, const ViewIndex &view,
                                     const ImVec2 &btn_min, int height_px)
        {
            const int row_count = view.row_count();
            float mouse_y = ImGui::GetIO().MousePos.y - btn_min.y;
            int target = static_cast<int>((mouse_y / static_cast<float>(height_px)) * row_count);
            target = std::clamp(target, 0, row_count - 1);
            const size_t src_line = view.row_to_source(target);

            if (ImGui::IsItemActive())
            {
                in.layout.pending_scroll_line = target;
                in.cursor.line = src_line;
                in.cursor.offset = 0;
                // Suppress autoscroll-to-cursor next frame; pending_scroll_line
                // already positions the view, and autoscroll would just redo it.
                in.layout.last_cursor_line = in.cursor.line;
                in.layout.last_cursor_offset = in.cursor.offset;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("line %zu", src_line + 1);
            }
        }

        float compute_row_size(int line_count, int height_px)
        {
            const float ratio = static_cast<float>(height_px) / static_cast<float>(line_count);
            if (line_count < height_px)
            {
                const float MAX_ROW_PX = 4.0F;
                return std::min(ratio, MAX_ROW_PX);
            }
            return ratio;
        }

        void draw_match_marks(ImDrawList *dl, const UiInputs &in, const ViewIndex &view,
                              const MinimapGeometry &geo, float row_size, float mark_h)
        {
            const int row_count = view.row_count();
            ImU32 match_col = ImGui::GetColorU32(ImVec4(
                in.view.color_match.x, in.view.color_match.y, in.view.color_match.z, 0.6F));
            for (size_t src : view.matched)
            {
                int row = view.source_to_row(src);
                if (row < 0)
                {
                    continue;
                }
                if (row >= row_count)
                {
                    break;
                }
                float y = geo.pos_y + row * row_size;
                dl->AddRectFilled(
                    ImVec2(geo.pos_x, y),
                    ImVec2(geo.pos_x + geo.width_px, y + mark_h),
                    match_col);
            }
        }

        void draw_viewport_indicator(ImDrawList *dl, const UiInputs &in,
                                     const MinimapGeometry &geo, int line_count, float row_size)
        {
            const int visible_count = in.layout.visible_line_last - in.layout.visible_line_first;
            if (visible_count >= line_count)
            {
                return;
            }
            const float MIN_VIEWPORT_H = 30.0F;
            float vp_h = std::max(static_cast<float>(visible_count) * row_size, MIN_VIEWPORT_H);
            float y = geo.pos_y + in.layout.visible_line_first * row_size;
            float max_y = geo.pos_y + geo.height_px - vp_h;
            y = std::min(y, max_y);
            dl->AddRectFilled(
                ImVec2(geo.pos_x, y),
                ImVec2(geo.pos_x + geo.width_px, y + vp_h),
                ImGui::GetColorU32(ImVec4(0.5F, 0.5F, 0.5F, 0.25F)));
        }

        void draw_cursor_mark(ImDrawList *dl, const UiInputs &in, const MinimapGeometry &geo,
                              int cursor_row, int line_count, float row_size, float mark_h)
        {
            if (cursor_row < 0 || cursor_row >= line_count)
            {
                return;
            }
            float cy = geo.pos_y + cursor_row * row_size;
            dl->AddRectFilled(
                ImVec2(geo.pos_x, cy),
                ImVec2(geo.pos_x + geo.width_px, cy + mark_h),
                ImGui::GetColorU32(in.view.color_scope));
        }

        void draw_minimap_contents(UiInputs &in, const ViewIndex &view,
                                   const MinimapGeometry &geo, int cursor_row)
        {
            ImDrawList *dl = ImGui::GetWindowDrawList();

            dl->AddRectFilled(
                ImVec2(geo.pos_x, geo.pos_y),
                ImVec2(geo.pos_x + geo.width_px, geo.pos_y + geo.height_px),
                ImGui::GetColorU32(ImVec4(0.0F, 0.0F, 0.0F, 0.15F)));

            ImVec2 btn_min = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##minimap_hit",
                                   ImVec2(geo.width_px, static_cast<float>(geo.height_px)));
            handle_wheel_scroll();

            const int row_count = view.row_count();
            if (row_count <= 0)
            {
                return;
            }

            handle_drag_and_tooltip(in, view, btn_min, geo.height_px);

            const float row_size = compute_row_size(row_count, geo.height_px);
            const float mark_h = std::max(row_size, 1.0F);

            draw_match_marks(dl, in, view, geo, row_size, mark_h);
            draw_viewport_indicator(dl, in, geo, row_count, row_size);
            draw_cursor_mark(dl, in, geo, cursor_row, row_count, row_size, mark_h);
        }
    } // namespace

    void draw_minimap(UiInputs &in, const SearchController &search)
    {
        if (!in.view.show_minimap)
        {
            return;
        }

        const MinimapGeometry geo = compute_geometry(in);
        const ViewIndex view = make_view_index(in, search);
        const int cursor_row = view.source_to_row(in.cursor.line);

        ImGui::SetNextWindowPos(ImVec2(geo.pos_x, geo.pos_y));
        ImGui::SetNextWindowSize(ImVec2(geo.width_px, static_cast<float>(geo.height_px)));

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##minimap", nullptr, flags))
        {
            draw_minimap_contents(in, view, geo, cursor_row);
        }
        ImGui::End();
    }

} // namespace kestrel
