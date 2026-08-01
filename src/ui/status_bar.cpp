#include "ui_internal.hpp"

#include "kestrel/search.hpp"

#include <cstdio>
#include <filesystem>

#include <imgui.h>

namespace kestrel
{
    namespace
    {
        void format_size(size_t bytes, char *out, size_t cap)
        {
            constexpr double KB = 1024.0;
            constexpr double MB = KB * 1024.0;
            constexpr double GB = MB * 1024.0;
            if (bytes >= static_cast<size_t>(GB))
            {
                std::snprintf(out, cap, "%.2f GB", bytes / GB);
            }
            else if (bytes >= static_cast<size_t>(MB))
            {
                std::snprintf(out, cap, "%.2f MB", bytes / MB);
            }
            else if (bytes >= static_cast<size_t>(KB))
            {
                std::snprintf(out, cap, "%.2f KB", bytes / KB);
            }
            else
            {
                std::snprintf(out, cap, "%zu B", bytes);
            }
        }
    } // namespace

    void draw_status_bar(UiInputs &in, const SearchController &search)
    {
        ImGuiViewport *vp = ImGui::GetMainViewport();
        const ImGuiStyle &style = ImGui::GetStyle();
        const float h = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0F;
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h));
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, h));

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##status_bar", nullptr, flags))
        {
            const auto progress = search.load_progress();
            if (search.is_loading())
            {
                const char *phase = progress.phase == SearchWorker::LoadPhase::IndexingLines ? "Indexing lines" :
                                    progress.phase == SearchWorker::LoadPhase::IndexingTimestamps ? "Indexing timestamps" :
                                    "Opening";
                ImGui::TextDisabled("%s", phase);
                if (progress.total > 0)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%zu / %zu", progress.completed, progress.total);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("| mapped source; index allocation in progress");
            }
            if (search.has_source())
            {
                if (search.is_loading())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("|");
                    ImGui::SameLine();
                }
                const size_t lines = search.line_index().line_count();
                const size_t bytes = search.source_bytes().size();
                char size_buf[32];
                format_size(bytes, size_buf, sizeof(size_buf));
                ImGui::Text("%zu lines", lines);
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                ImGui::TextUnformatted(size_buf);
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                ImGui::Text("%zu matches", search.matches().size());
                ImGui::SameLine();
                if (search.is_compiling())
                    ImGui::TextDisabled("Scanning…");
                else
                    ImGui::TextDisabled("scan %.2f ms", search.last_scan_ms());
                ImGui::SameLine();
                ImGui::TextDisabled("| mmap %s + index %zu KB",
                                    size_buf,
                                    (search.line_index().memory_bytes() + search.timestamp_index().memory_bytes()) / 1024);
                if (!in.file_load.current_path.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("|");
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", in.file_load.current_path.c_str());
                }
            }
            else
            {
                ImGui::TextDisabled("No file loaded");
            }
            in.layout.status_bar_h = ImGui::GetWindowHeight();
        }
        ImGui::End();
    }

} // namespace kestrel
