#include "kestrel/ui.hpp"

#include <imgui.h>

namespace kestrel {

  static void draw_main_menu(UiState& state) {
      if (ImGui::BeginMainMenuBar()) {
          if (ImGui::BeginMenu("File")) {
              if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                  // TODO: trigger file open
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

  void draw_ui(UiState& state) {
      draw_main_menu(state);

      if (state.show_demo) {
          ImGui::ShowDemoWindow(&state.show_demo);
      }

      // add panels here
  }

} // namespace kestrel
