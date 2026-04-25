#include "ui_internal.hpp"

#include "kestrel/search.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>

#include <imgui.h>

namespace kestrel
{

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
        {
            std::memcpy(query_backup, in.search.query, sizeof(in.search.query));
        }

        ImGui::InputTextWithHint("##query", "search...", in.search.query, IM_ARRAYSIZE(in.search.query));

        if (esc_pressed && ImGui::IsItemDeactivated())
        {
            std::memcpy(in.search.query, query_backup, sizeof(in.search.query));
        }
    }

    static void draw_toolbar(UiInputs &in, const SearchController &search)
    {
        ImGui::Checkbox("Aa", &in.search.case_sensitive);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Case sensitive");
        }

        ImGui::SameLine();
        ImGui::Checkbox(".*", &in.search.dotall);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Dot matches newlines\n(. matches \\n and all characters)");
        }

        ImGui::SameLine();
        ImGui::Checkbox("^$", &in.search.multiline);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Multiline anchors\n(^ and $ match line boundaries)");
        }

        ImGui::SameLine();
        ImGui::Text("%d before", in.layout.matches_before);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Matches before cursor");
        }

        ImGui::SameLine();
        ImGui::Text("%d after", in.layout.matches_after);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Matches after cursor");
        }

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
            ImGui::TextColored(ImVec4(1.0F, 0.35F, 0.35F, 1.0F), "%s", search.compile_error().c_str());
        }
    }

    static void format_time_label(int64_t epoch, bool include_date, char *out, size_t cap)
    {
        time_t t = (time_t)epoch;
        struct tm g;
        gmtime_r(&t, &g);
        if (include_date)
        {
            strftime(out, cap, "%Y-%m-%d %H:%M:%S", &g);
        }
        else
        {
            strftime(out, cap, "%H:%M:%S", &g);
        }
    }

    static void draw_time_range(UiInputs &in, const SearchController &search)
    {
        const TimestampIndex &ts = search.timestamp_index();
        if (ts.empty())
        {
            return;
        }
        // Needs ~500px for checkbox + two 200px sliders. Skip if remaining
        // toolbar width can't hold them, so widgets don't wrap/overflow.
        const float s = ui_scale();
        if (ImGui::GetContentRegionAvail().x < 500.0F * s)
        {
            return;
        }

        auto &tf = in.layout.filters.time;

        // Reset bounds on new file / new timestamp range.
        if (ts.min_ts() != tf.source_min || ts.max_ts() != tf.source_max)
        {
            tf.source_min = ts.min_ts();
            tf.source_max = ts.max_ts();
            tf.start = ts.min_ts();
            tf.end = ts.max_ts();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Checkbox("time", &tf.active);
        if (!tf.active)
        {
            return;
        }

        const bool span_multiday = (ts.max_ts() - ts.min_ts()) >= 24 * 3600;
        char sbuf[32], ebuf[32];
        format_time_label(tf.start, span_multiday, sbuf, sizeof(sbuf));
        format_time_label(tf.end, span_multiday, ebuf, sizeof(ebuf));

        int64_t lo = ts.min_ts();
        int64_t hi = ts.max_ts();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0F * s);
        ImGui::SliderScalar("##ts_start", ImGuiDataType_S64,
                            &tf.start, &lo, &tf.end, sbuf);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0F * s);
        ImGui::SliderScalar("##ts_end", ImGuiDataType_S64,
                            &tf.end, &tf.start, &hi, ebuf);
    }

    void draw_search_bar(UiInputs &in, const SearchController &)
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
            const float gear_w = ImGui::CalcTextSize(" * ").x + ImGui::GetStyle().FramePadding.x * 2.0F;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - gear_w - ImGui::GetStyle().ItemSpacing.x);
            draw_query_input(in);
            ImGui::SameLine();
            if (ImGui::Button(" * "))
            {
                in.show_settings = !in.show_settings;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Settings");
            }
            in.layout.search_bar_h = ImGui::GetWindowHeight();
        }
        ImGui::End();
    }

    void draw_toolbar_row(UiInputs &in, const SearchController &search)
    {
        ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + in.layout.search_bar_h));
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 0));

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##toolbar", nullptr, flags))
        {
            draw_toolbar(in, search);
            draw_time_range(in, search);
            in.layout.toolbar_h = ImGui::GetWindowHeight();
        }
        ImGui::End();
    }

} // namespace kestrel
