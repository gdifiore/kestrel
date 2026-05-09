#include "ui_internal.hpp"

#include "kestrel/line_index.hpp"
#include "kestrel/search.hpp"

#include <algorithm>
#include <cstdlib>

#include <imgui.h>
#include <ImGuiFileDialog.h>

namespace kestrel
{

    static void draw_shortcut_row(const char *key, const char *action)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.7F, 0.7F, 1.0F, 1.0F), "%s", key);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(action);
    }

    static void draw_shortcuts_table()
    {
        if (!ImGui::BeginTable("shortcuts", 2, ImGuiTableFlags_SizingFixedFit))
        {
            return;
        }
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 80.0F * ui_scale());
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);

        draw_shortcut_row("Ctrl+F", "Focus search box");
        draw_shortcut_row("Ctrl+O", "Open file dialog");
        draw_shortcut_row("Ctrl+Q", "Quit application");
        draw_shortcut_row("Escape", "Unfocus input");
        draw_shortcut_row("Ctrl+L", "Clear search");
        draw_shortcut_row("n", "Next match");
        draw_shortcut_row("Shift+N", "Previous match");
        draw_shortcut_row("↑↓ PgUp/Dn", "Navigate lines");
        draw_shortcut_row("Home/End", "First/last line");
        draw_shortcut_row("Ctrl+G", "Go to line");
        draw_shortcut_row("Shift+Click", "Extend line selection");
        draw_shortcut_row("Shift+↑↓/PgUp/Dn", "Extend line selection");
        draw_shortcut_row("Ctrl+C", "Copy selection (or search pattern)");
        draw_shortcut_row("Ctrl+V", "Paste to search");

        ImGui::EndTable();
    }

    static void draw_vim_shortcuts_table()
    {
        if (!ImGui::BeginTable("vim_shortcuts", 2, ImGuiTableFlags_SizingFixedFit))
        {
            return;
        }
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 80.0F * ui_scale());
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);

        draw_shortcut_row("j / k", "Down / up one line");
        draw_shortcut_row("gg / G", "First / last line");
        draw_shortcut_row("Ctrl+d/u", "Half page down / up");
        draw_shortcut_row("/", "Focus search");
        draw_shortcut_row(":", "Go to line");
        draw_shortcut_row("v", "Toggle line selection");
        draw_shortcut_row("y", "Yank selection (or line)");

        ImGui::EndTable();
    }

    void draw_settings_popup(UiInputs &in)
    {
        if (!in.show_settings)
        {
            return;
        }
        if (ImGui::Begin("Settings", &in.show_settings, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::SeparatorText("Display");
            ImGui::Checkbox("Show minimap", &in.view.show_minimap);
            ImGui::Checkbox("Tint by log level", &in.view.log_level_tint);
            ImGui::Checkbox("Snap scroll to lines", &in.view.snap_scroll);
            ImGui::Checkbox("Show only filtered results", &in.view.display_only_filtered_lines);
            ImGui::Checkbox("Color each regex capture group individually", &in.view.highlight_groups);

            ImGui::SeparatorText("Input");
            ImGui::Checkbox("Vim mode", &in.view.vim_mode);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Enable vim-style navigation (j/k, gg, G, /, v, y, Ctrl+d/u)\nActive when no input widget is focused");
            }

            ImGui::SeparatorText("Regex Flags");
            ImGui::Checkbox("Case sensitive", &in.search.case_sensitive);

            ImGui::Checkbox("Dot matches newlines", &in.search.dotall);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Make . (dot) match newline characters\nPattern 'foo.*bar' can match across lines");
            }

            ImGui::Checkbox("Multiline anchors", &in.search.multiline);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Make ^ and $ match line boundaries\n^ = start of line, $ = end of line");
            }

            ImGui::SeparatorText("Keyboard Shortcuts");
            draw_shortcuts_table();

            if (in.view.vim_mode)
            {
                ImGui::SeparatorText("Vim Shortcuts");
                draw_vim_shortcuts_table();
            }
        }
        ImGui::End();
    }

    void draw_open_dialog(UiInputs &in)
    {
        auto *dlg = ImGuiFileDialog::Instance();
        const float s = ui_scale();
        ImVec2 min_size(600.0F * s, 400.0F * s);
        if (dlg->Display("kestrel_open", ImGuiWindowFlags_NoCollapse, min_size))
        {
            if (dlg->IsOk())
            {
                in.file_load.pending_open = dlg->GetFilePathName();
            }
            dlg->Close();
        }
    }

    void draw_goto_line_dialog(UiInputs &in, const SearchController &search)
    {
        if (!in.hotkeys.show_goto_line)
        {
            return;
        }

        if (ImGui::Begin("Go to Line", &in.hotkeys.show_goto_line, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
        {
            ImGui::Text("Enter line number:");
            ImGui::SetNextItemWidth(200.0F * ui_scale());

            bool enter_pressed = ImGui::InputText("##line", in.hotkeys.goto_line_input, sizeof(in.hotkeys.goto_line_input), ImGuiInputTextFlags_EnterReturnsTrue);

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
                long line_num = strtol(in.hotkeys.goto_line_input, &endptr, 10);

                if (*endptr == '\0' && line_num > 0 && search.has_source())
                {
                    // Convert to 0-based and clamp to valid range
                    size_t target_line = static_cast<size_t>(line_num - 1);
                    size_t max_line = search.line_index().line_count();
                    if (max_line > 0)
                    {
                        target_line = std::min(target_line, max_line - 1);
                        in.cursor.line = target_line;
                        in.cursor.offset = search.line_index().line_start(target_line);
                    }
                }

                // Close dialog and clear input
                in.hotkeys.show_goto_line = false;
                in.hotkeys.goto_line_input[0] = '\0';
            }

            if (cancel_button)
            {
                in.hotkeys.show_goto_line = false;
                in.hotkeys.goto_line_input[0] = '\0';
            }
        }
        ImGui::End();
    }

} // namespace kestrel
