#pragma once

#include <imgui.h>
#include <string>

namespace kestrel {

  struct UiState {
      bool show_demo = false;
      bool quit_requested = false;
      std::string pending_open;

      // search bar
      char query[512]      = {};
      bool case_sensitive  = false;
      int  matches_before      = 2;
      int  matches_after       = 2;
      bool show_line_nums  = true;
      bool show_settings   = false;
      ImVec4 color_match   = ImVec4(1.00f, 0.85f, 0.20f, 1.00f);
      ImVec4 color_scope   = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
  };

  void draw_ui(UiState& state);

} // namespace kestrel
