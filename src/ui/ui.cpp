#include "kestrel/ui.hpp"

#include "kestrel/line_index.hpp"
#include "kestrel/search.hpp"

#include <cmath>
#include <cstddef>
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
                ImGui::Separator();
                ImGui::MenuItem("ImGui Demo", nullptr, &in.show_demo);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
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
            ImGui::ColorEdit4("scope", &in.color_scope.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            ImGui::SameLine();
            ImGui::TextUnformatted("scope");

            ImGui::SameLine();
            ImGui::Checkbox("line #", &in.show_line_nums);

            const char *hint = "Ctrl+F  Ctrl+O  Ctrl+Q";
            float hint_w = ImGui::CalcTextSize(hint).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - hint_w - ImGui::GetStyle().WindowPadding.x);
            ImGui::TextDisabled("%s", hint);

            if (!search.compile_error().empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", search.compile_error().c_str());
            }

            if (in.search_bar_h == 0.0f)
                in.search_bar_h = ImGui::GetFrameHeightWithSpacing() * 2.0f;
        }
        ImGui::End();
    }

    static void draw_results(UiInputs &in, const SearchController &search)
    {
        ImGuiViewport *vp = ImGui::GetMainViewport();
        float window_pos_y = vp->WorkPos.y + in.search_bar_h;
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, window_pos_y));
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - in.search_bar_h));

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##results", nullptr, flags))
        {
            if (in.snap_scroll)
            {
                float line_h = ImGui::GetTextLineHeightWithSpacing();
                float scroll_y = ImGui::GetScrollY();
                float snapped = std::floor(scroll_y / line_h) * line_h;
                if (snapped != scroll_y)
                    ImGui::SetScrollY(snapped);
            }

            const bool has_source = search.has_source();
            auto source_bytes = has_source ? search.source_bytes() : std::span<const char>{};

            if (!has_source || source_bytes.empty())
            {
                ImGui::TextDisabled("No file loaded. Drop a file or use File > Open...");
            }
            else
            {
                const LineIndex &lines = search.line_index();
                const int total_lines = static_cast<int>(lines.line_count());
                const bool filtered = !search.pattern_empty();
                const auto &matched = search.matched_lines();
                const int view_count = filtered ? static_cast<int>(matched.size()) : total_lines;

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
                        const int line_idx = filtered
                                                 ? static_cast<int>(matched[i])
                                                 : i;
                        if (in.show_line_nums)
                        {
                            ImGui::TextDisabled("%7d ", line_idx + 1);
                            ImGui::SameLine();
                        }
                        std::size_t start = lines.line_start(line_idx);
                        std::size_t end = (line_idx + 1 < total_lines)
                                              ? lines.line_start(line_idx + 1)
                                              : source_bytes.size();
                        while (end > start && (source_bytes[end - 1] == '\n' || source_bytes[end - 1] == '\r'))
                            --end;
                        const char *p = source_bytes.data() + start;
                        ImGui::TextUnformatted(p, p + (end - start));
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
        if (ImGui::Begin("Settings", &in.show_settings))
        {
            ImGui::Checkbox("Snap scroll to lines", &in.snap_scroll);
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

    void draw_ui(UiInputs &in, const SearchController &search)
    {
        draw_main_menu(in);
        draw_search_bar(in, search);
        draw_results(in, search);
        draw_settings_popup(in);
        draw_open_dialog(in);

        if (in.show_demo)
        {
            ImGui::ShowDemoWindow(&in.show_demo);
        }
    }

} // namespace kestrel
