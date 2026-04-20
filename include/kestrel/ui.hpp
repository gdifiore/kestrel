#pragma once

#include <imgui.h>
#include <optional>
#include <string>
#include <vector>

namespace kestrel
{

    class SearchController;

    // UI-owned state: user inputs + layout scratch. Derived/view state
    // (matches, source bytes, compile errors) is read live from SearchController.
    struct UiInputs
    {
        bool show_demo = false;
        bool quit_requested = false;
        std::optional<std::string> pending_open;

        // Keyboard shortcut triggers
        bool focus_search = false;
        bool trigger_open_dialog = false;

        // File loading state
        bool loading = false;
        std::string loading_path;
        std::string loading_error;

        // Recent files (most recent first)
        std::vector<std::string> recent_files;

        size_t cursor_line = 0;     // Current line number
        size_t cursor_offset = 0;   // Byte offset in source
        bool cursor_visible = true; // Show cursor highlight

        char query[512] = {};
        bool case_sensitive = false;
        bool dotall = true;
        bool multiline = false;
        bool show_line_nums = true;
        bool show_settings = false;
        bool snap_scroll = true;
        bool is_dark_mode = true;
        ImVec4 color_match = ImVec4(1.00f, 0.85f, 0.20f, 1.00f);
        ImVec4 color_scope = ImVec4(0.44f, 0.66f, 0.84f, 1.00f); // Matches previous HeaderHovered cursor color

        float search_bar_h = 0.0f;

        // cursor-relative counts, fed from controller each frame
        int matches_before = 0;
        int matches_after = 0;
    };

    void draw_ui(UiInputs &inputs, const SearchController &search);

} // namespace kestrel
