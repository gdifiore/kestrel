#include "ui_internal.hpp"

#include "kestrel/search.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <imgui.h>

namespace kestrel
{

    static void remember_query(UiInputs &in)
    {
        const std::string query(in.search.query);
        if (query.empty())
            return;
        std::erase(in.search.history, query);
        in.search.history.insert(in.search.history.begin(), query);
        if (in.search.history.size() > 10)
            in.search.history.resize(10);
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
        {
            std::memcpy(query_backup, in.search.query, sizeof(in.search.query));
        }

        const bool submitted = ImGui::InputTextWithHint(
            "##query", "Search regular expression…", in.search.query,
            IM_ARRAYSIZE(in.search.query), ImGuiInputTextFlags_EnterReturnsTrue);

        if (esc_pressed && ImGui::IsItemDeactivated())
        {
            std::memcpy(in.search.query, query_backup, sizeof(in.search.query));
        }
        if (submitted)
            remember_query(in);
    }

    static void draw_time_range(UiInputs &in, const SearchController &search);

    static void draw_search_options(UiInputs &in, const SearchController &search)
    {
        if (!ImGui::BeginPopup("search_options"))
            return;

        ImGui::TextUnformatted("Regex options");
        ImGui::Separator();
        ImGui::Checkbox("Smart case", &in.search.smart_case);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Uppercase letters make a pattern case-sensitive");
        ImGui::BeginDisabled(in.search.smart_case);
        ImGui::Checkbox("Case sensitive", &in.search.case_sensitive);
        ImGui::EndDisabled();
        ImGui::Checkbox("Dot matches newlines", &in.search.dotall);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Dot matches newlines\n(. matches \\n and all characters)");
        ImGui::Checkbox("Multiline anchors", &in.search.multiline);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Multiline anchors\n(^ and $ match line boundaries)");
        ImGui::SeparatorText("Time range");
        draw_time_range(in, search);
        ImGui::EndPopup();
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
        const float s = ui_scale();

        auto &tf = in.layout.filters.time;

        // Reset bounds on new file / new timestamp range.
        if (ts.min_ts() != tf.source_min || ts.max_ts() != tf.source_max)
        {
            tf.source_min = ts.min_ts();
            tf.source_max = ts.max_ts();
            tf.start = ts.min_ts();
            tf.end = ts.max_ts();
        }

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
        ImGui::SetNextItemWidth(200.0F * s);
        ImGui::SliderScalar("##ts_start", ImGuiDataType_S64,
                            &tf.start, &lo, &tf.end, sbuf);
        ImGui::SetNextItemWidth(200.0F * s);
        ImGui::SliderScalar("##ts_end", ImGuiDataType_S64,
                            &tf.end, &tf.start, &hi, ebuf);
    }

    void draw_search_bar(UiInputs &in, const SearchController &search)
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
            const ImGuiStyle &style = ImGui::GetStyle();
            const auto button_width = [&style](const char *label)
            { return ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0F; };
            const float controls_width = button_width("Clear") + button_width("Options") +
                                         button_width("Patterns") + std::max(button_width("Pin"), button_width("Unpin")) +
                                         style.ItemSpacing.x * 4.0F;
            ImGui::SetNextItemWidth(std::max(120.0F * ui_scale(), ImGui::GetContentRegionAvail().x - controls_width));
            draw_query_input(in);
            ImGui::SameLine();
            if (ImGui::Button("Clear"))
            {
                in.search.query[0] = '\0';
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Clear search (Ctrl+L)");
            ImGui::SameLine();
            if (ImGui::Button("Options"))
                ImGui::OpenPopup("search_options");
            draw_search_options(in, search);
            ImGui::SameLine();
            if (ImGui::BeginCombo("##patterns", "Patterns"))
            {
                if (!in.search.pinned_queries.empty())
                {
                    ImGui::TextDisabled("Pinned");
                    for (const std::string &query : in.search.pinned_queries)
                    {
                        if (ImGui::Selectable(query.c_str()))
                            std::snprintf(in.search.query, sizeof(in.search.query), "%s", query.c_str());
                    }
                    ImGui::Separator();
                }
                ImGui::TextDisabled("Recent this session");
                for (const std::string &query : in.search.history)
                {
                    if (ImGui::Selectable(query.c_str()))
                        std::snprintf(in.search.query, sizeof(in.search.query), "%s", query.c_str());
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            const std::string query(in.search.query);
            const bool pinned = std::find(in.search.pinned_queries.begin(), in.search.pinned_queries.end(), query) != in.search.pinned_queries.end();
            ImGui::BeginDisabled(query.empty());
            if (ImGui::Button(pinned ? "Unpin" : "Pin"))
            {
                if (pinned)
                    std::erase(in.search.pinned_queries, query);
                else
                    in.search.pinned_queries.push_back(query);
            }
            ImGui::EndDisabled();

            if (!search.compile_error().empty())
                ImGui::TextColored(ImVec4(1.0F, 0.35F, 0.35F, 1.0F), "%s", search.compile_error().c_str());
            in.layout.search_bar_h = ImGui::GetWindowHeight();
        }
        ImGui::End();
    }

    void draw_toolbar_row(UiInputs &in, const SearchController &search)
    {
        (void)search;
        in.layout.toolbar_h = 0.0F;
    }

} // namespace kestrel
