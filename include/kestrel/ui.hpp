#pragma once

#include "kestrel/line_index.hpp"

#include <cstddef>
#include <imgui.h>
#include <span>
#include <string>
#include <vector>

namespace kestrel {

  struct UiState {
      bool show_demo = false;
      bool quit_requested = false;
      std::string pending_open;

      // search bar
      char query[512]      = {};
      bool case_sensitive  = false;
      int  matches_before      = 0;
      int  matches_after       = 0;
      bool show_line_nums  = true;
      bool show_settings   = false;
      bool snap_scroll     = true;
      ImVec4 color_match   = ImVec4(1.00f, 0.85f, 0.20f, 1.00f);
      ImVec4 color_scope   = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);

      float  search_bar_h  = 0.0f; // measured each frame

      // non-owning view of current loaded source + line index
      std::span<const char> source_bytes;
      const LineIndex* lines = nullptr;

      // filtered view: if non-null and non-empty, show only these line indices.
      // if non-null but empty AND pattern_active, show nothing (no matches).
      // if null, show all lines.
      const std::vector<std::size_t>* visible_lines = nullptr;
      bool pattern_active = false;

      std::string compile_error;
      std::size_t match_count = 0;
      double scan_ms = 0.0;
  };

  void draw_ui(UiState& state);

} // namespace kestrel
