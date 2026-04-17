#pragma once

namespace kestrel {

  struct UiState {
      bool show_demo = false;
      bool quit_requested = false;
      // add UI state here (open file path, filter text, etc.)
  };

  void draw_ui(UiState& state);

} // namespace kestrel
