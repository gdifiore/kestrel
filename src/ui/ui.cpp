#include "kestrel/ui.hpp"

#include <imgui.h>
#include <ImGuiFileDialog.h>

namespace kestrel {

  static void draw_main_menu(UiState& state) {
      if (ImGui::BeginMainMenuBar()) {
          if (ImGui::BeginMenu("File")) {
              if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                  IGFD::FileDialogConfig cfg;
                  cfg.path = ".";
                  ImGuiFileDialog::Instance()->OpenDialog(
                      "kestrel_open", "Open file", ".*,.txt,.log,.md", cfg);
              }
              ImGui::Separator();
              if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
                  state.quit_requested = true;
              }
              ImGui::EndMenu();
          }
          if (ImGui::BeginMenu("View")) {
              ImGui::MenuItem("ImGui Demo", nullptr, &state.show_demo);
              ImGui::EndMenu();
          }
          ImGui::EndMainMenuBar();
      }
  }

  static void draw_search_bar(UiState& state) {
      ImGuiViewport* vp = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
      ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 0));

      ImGuiWindowFlags flags =
          ImGuiWindowFlags_NoDecoration |
          ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoFocusOnAppearing |
          ImGuiWindowFlags_NoBringToFrontOnFocus;

      if (ImGui::Begin("##search_bar", nullptr, flags)) {
          // --- row 1: query + gear ---
          const float gear_w = ImGui::CalcTextSize(" * ").x + ImGui::GetStyle().FramePadding.x * 2.0f;
          ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - gear_w - ImGui::GetStyle().ItemSpacing.x);
          ImGui::InputTextWithHint("##query", "search...", state.query, IM_ARRAYSIZE(state.query));
          ImGui::SameLine();
          if (ImGui::Button(" * ")) state.show_settings = !state.show_settings;
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Settings");

          // --- row 2: options ---
          ImGui::Checkbox("Aa", &state.case_sensitive);
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Case sensitive");

          ImGui::SameLine();
          ImGui::Text("%d before", state.matches_before);
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Matches before cursor");

          ImGui::SameLine();
          ImGui::Text("%d after", state.matches_after);
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Matches after cursor");

          ImGui::SameLine();
          ImGui::ColorEdit4("match", &state.color_match.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
          ImGui::SameLine(); ImGui::TextUnformatted("match");

          ImGui::SameLine();
          ImGui::ColorEdit4("scope", &state.color_scope.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
          ImGui::SameLine(); ImGui::TextUnformatted("scope");

          ImGui::SameLine();
          ImGui::Checkbox("line #", &state.show_line_nums);

          // right-aligned shortcut placeholder
          const char* hint = "Ctrl+F  Ctrl+O  Ctrl+Q";
          float hint_w = ImGui::CalcTextSize(hint).x;
          ImGui::SameLine(ImGui::GetWindowWidth() - hint_w - ImGui::GetStyle().WindowPadding.x);
          ImGui::TextDisabled("%s", hint);
      }
      ImGui::End();
  }

  static void draw_settings_popup(UiState& state) {
      if (!state.show_settings) return;
      if (ImGui::Begin("Settings", &state.show_settings)) {
          ImGui::TextUnformatted("TODO: settings go here");
      }
      ImGui::End();
  }

  static void draw_open_dialog(UiState& state) {
      auto* dlg = ImGuiFileDialog::Instance();
      ImVec2 min_size(600, 400);
      if (dlg->Display("kestrel_open", ImGuiWindowFlags_NoCollapse, min_size)) {
          if (dlg->IsOk()) {
              state.pending_open = dlg->GetFilePathName();
          }
          dlg->Close();
      }
  }

  void draw_ui(UiState& state) {
      draw_main_menu(state);
      draw_search_bar(state);
      draw_settings_popup(state);
      draw_open_dialog(state);

      if (state.show_demo) {
          ImGui::ShowDemoWindow(&state.show_demo);
      }
  }

} // namespace kestrel
