#include "ui_internal.hpp"

#include "kestrel/config.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>

#include <imgui.h>
#include <ImGuiFileDialog.h>

namespace kestrel
{

    void draw_main_menu(UiInputs &in)
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
                if (!in.file_prefs.recent_files.empty() && ImGui::BeginMenu("Recent Files"))
                {
                    // Clean up non-existent files first
                    cleanup_recent_files(in);

                    for (size_t i = 0; i < in.file_prefs.recent_files.size(); ++i)
                    {
                        const std::string &path = in.file_prefs.recent_files[i];

                        // Show just filename, full path in tooltip
                        std::filesystem::path file_path(path);
                        std::string display_name = file_path.filename().string();

                        if (ImGui::MenuItem(display_name.c_str()))
                        {
                            in.file_load.pending_open = path;
                        }

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("%s", path.c_str());
                        }
                    }

                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear Recent"))
                    {
                        in.file_prefs.recent_files.clear();
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

} // namespace kestrel
