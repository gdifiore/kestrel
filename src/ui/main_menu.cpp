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
    namespace
    {
        void open_file_dialog()
        {
            IGFD::FileDialogConfig cfg;
            cfg.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog(
                "kestrel_open", "Open file", ".*,.txt,.log,.md", cfg);
        }

        void draw_recent_files_submenu(UiInputs &in)
        {
            if (in.file_prefs.recent_files.empty() || !ImGui::BeginMenu("Recent Files"))
            {
                return;
            }

            cleanup_recent_files(in);

            for (const std::string &path : in.file_prefs.recent_files)
            {
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

        void draw_file_menu(UiInputs &in)
        {
            if (!ImGui::BeginMenu("File"))
            {
                return;
            }

            if (ImGui::MenuItem("Open...", "Ctrl+O"))
            {
                open_file_dialog();
            }

            draw_recent_files_submenu(in);

            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Ctrl+Q"))
            {
                in.quit_requested = true;
            }
            ImGui::EndMenu();
        }

        void draw_view_menu(UiInputs &in)
        {
            if (!ImGui::BeginMenu("View"))
            {
                return;
            }

            float &scale = ImGui::GetIO().FontGlobalScale;
            if (ImGui::MenuItem("Zoom In", "Ctrl+="))
            {
                scale = std::min(scale + 0.1F, 3.0F);
            }
            if (ImGui::MenuItem("Zoom Out", "Ctrl+-"))
            {
                scale = std::max(scale - 0.1F, 0.5F);
            }
            if (ImGui::MenuItem("Reset Zoom", "Ctrl+0"))
            {
                scale = 1.0F;
            }
            if (ImGui::MenuItem("Dark Mode", nullptr, &in.view.is_dark_mode))
            {
                if (in.view.is_dark_mode)
                {
                    ImGui::StyleColorsDark();
                }
                else
                {
                    ImGui::StyleColorsLight();
                }
            }
            ImGui::EndMenu();
        }
    } // namespace

    void draw_main_menu(UiInputs &in)
    {
        if (ImGui::BeginMainMenuBar())
        {
            draw_file_menu(in);
            draw_view_menu(in);
            ImGui::EndMainMenuBar();
        }

        if (in.hotkeys.trigger_open_dialog)
        {
            open_file_dialog();
            in.hotkeys.trigger_open_dialog = false;
        }
    }

} // namespace kestrel
