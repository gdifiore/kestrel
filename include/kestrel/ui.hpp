#pragma once

#include <imgui.h>
#include <optional>
#include <string>

namespace kestrel {

  class SearchController;

  // UI-owned state: user inputs + layout scratch. Derived/view state
  // (matches, source bytes, compile errors) is read live from SearchController.
  struct UiInputs {
      bool show_demo = false;
      bool quit_requested = false;
      std::optional<std::string> pending_open;

      char query[512]      = {};
      bool case_sensitive  = false;
      bool show_line_nums  = true;
      bool show_settings   = false;
      bool snap_scroll     = true;
      ImVec4 color_match   = ImVec4(1.00f, 0.85f, 0.20f, 1.00f);
      ImVec4 color_scope   = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);

      float  search_bar_h  = 0.0f;

      // cursor-relative counts, fed from controller each frame
      int matches_before   = 0;
      int matches_after    = 0;
  };

  void draw_ui(UiInputs& inputs, const SearchController& search);

} // namespace kestrel
