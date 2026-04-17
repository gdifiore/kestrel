#include "kestrel/ui.hpp"

#include <cmath>

#include <imgui.h>
#include <ImGuiFileDialog.h>

namespace kestrel
{

    static void draw_main_menu(UiState &state)
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
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Ctrl+Q"))
                {
                    state.quit_requested = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                float& scale = ImGui::GetIO().FontGlobalScale;
                if (ImGui::MenuItem("Zoom In",  "Ctrl+=")) scale = std::min(scale + 0.1f, 3.0f);
                if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) scale = std::max(scale - 0.1f, 0.5f);
                if (ImGui::MenuItem("Reset Zoom", "Ctrl+0")) scale = 1.0f;
                ImGui::Separator();
                ImGui::MenuItem("ImGui Demo", nullptr, &state.show_demo);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    static void draw_search_bar(UiState &state)
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
            // --- row 1: query + gear ---
            const float gear_w = ImGui::CalcTextSize(" * ").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - gear_w - ImGui::GetStyle().ItemSpacing.x);
            ImGui::InputTextWithHint("##query", "search...", state.query, IM_ARRAYSIZE(state.query));
            ImGui::SameLine();
            if (ImGui::Button(" * "))
                state.show_settings = !state.show_settings;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Settings");

            // --- row 2: options ---
            ImGui::Checkbox("Aa", &state.case_sensitive);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Case sensitive");

            ImGui::SameLine();
            ImGui::Text("%d before", state.matches_before);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Matches before cursor");

            ImGui::SameLine();
            ImGui::Text("%d after", state.matches_after);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Matches after cursor");

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::Text("%zu matches", state.match_count);
            ImGui::SameLine();
            ImGui::TextDisabled("%.2f ms", state.scan_ms);

            ImGui::SameLine();
            ImGui::ColorEdit4("match", &state.color_match.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            ImGui::SameLine();
            ImGui::TextUnformatted("match");

            ImGui::SameLine();
            ImGui::ColorEdit4("scope", &state.color_scope.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            ImGui::SameLine();
            ImGui::TextUnformatted("scope");

            ImGui::SameLine();
            ImGui::Checkbox("line #", &state.show_line_nums);

            // right-aligned shortcut placeholder
            const char *hint = "Ctrl+F  Ctrl+O  Ctrl+Q";
            float hint_w = ImGui::CalcTextSize(hint).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - hint_w - ImGui::GetStyle().WindowPadding.x);
            ImGui::TextDisabled("%s", hint);

            if (!state.compile_error.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", state.compile_error.c_str());
            }

            state.search_bar_h = ImGui::GetWindowHeight();
        }
        ImGui::End();
    }

    static void draw_results(UiState &state)
    {
        ImGuiViewport *vp = ImGui::GetMainViewport();
        float y = vp->WorkPos.y + state.search_bar_h;
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, y));
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - state.search_bar_h));

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##results", nullptr, flags))
        {
            if (state.snap_scroll)
            {
                float line_h = ImGui::GetTextLineHeightWithSpacing();
                float y = ImGui::GetScrollY();
                float snapped = std::floor(y / line_h) * line_h;
                if (snapped != y)
                    ImGui::SetScrollY(snapped);
            }

            if (!state.lines || state.source_bytes.empty())
            {
                ImGui::TextDisabled("No file loaded. Drop a file or use File > Open...");
            }
            else
            {
                const int total_lines = static_cast<int>(state.lines->line_count());
                const bool filtered = state.pattern_active;
                const int view_count = filtered
                    ? (state.visible_lines ? static_cast<int>(state.visible_lines->size()) : 0)
                    : total_lines;

                if (filtered && view_count == 0 && state.compile_error.empty())
                {
                    ImGui::TextDisabled("No matches.");
                }

                ImGuiListClipper clipper;
                clipper.Begin(view_count);
                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    {
                        const int line_idx = filtered
                            ? static_cast<int>((*state.visible_lines)[i])
                            : i;
                        if (state.show_line_nums)
                        {
                            ImGui::TextDisabled("%7d ", line_idx + 1);
                            ImGui::SameLine();
                        }
                        std::size_t start = state.lines->line_start(line_idx);
                        std::size_t end = (line_idx + 1 < total_lines)
                            ? state.lines->line_start(line_idx + 1)
                            : state.source_bytes.size();
                        while (end > start && (state.source_bytes[end - 1] == '\n'
                                            || state.source_bytes[end - 1] == '\r'))
                            --end;
                        const char* p = state.source_bytes.data() + start;
                        ImGui::TextUnformatted(p, p + (end - start));
                    }
                }
            }
        }
        ImGui::End();
    }

    static void draw_settings_popup(UiState &state)
    {
        if (!state.show_settings)
            return;
        if (ImGui::Begin("Settings", &state.show_settings))
        {
            ImGui::Checkbox("Snap scroll to lines", &state.snap_scroll);
        }
        ImGui::End();
    }

    static void draw_open_dialog(UiState &state)
    {
        auto *dlg = ImGuiFileDialog::Instance();
        ImVec2 min_size(600, 400);
        if (dlg->Display("kestrel_open", ImGuiWindowFlags_NoCollapse, min_size))
        {
            if (dlg->IsOk())
            {
                state.pending_open = dlg->GetFilePathName();
            }
            dlg->Close();
        }
    }

    void draw_ui(UiState &state)
    {
        draw_main_menu(state);
        draw_search_bar(state);
        draw_results(state);
        draw_settings_popup(state);
        draw_open_dialog(state);

        if (state.show_demo)
        {
            ImGui::ShowDemoWindow(&state.show_demo);
        }
    }

} // namespace kestrel
