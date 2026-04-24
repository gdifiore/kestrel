#pragma once

#include "kestrel/group_matcher.hpp"

#include <imgui.h>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace kestrel
{
    inline constexpr int MINIMAP_WIDTH = 80; // px
    class SearchController;

    struct SearchInputs
    {
        char query[512] = {};
        bool case_sensitive = false;
        bool dotall = true;
        bool multiline = false;
    };

    struct ViewPrefs
    {
        bool show_minimap = true;
        bool highlight_groups = true;
        bool display_only_filtered_lines = false;
        bool show_line_nums = true;
        bool snap_scroll = true;
        bool is_dark_mode = true;
        bool log_level_tint = true;
        ImVec4 color_match = ImVec4(1.00f, 0.85f, 0.20f, 1.00f);
        ImVec4 color_scope = ImVec4(0.44f, 0.66f, 0.84f, 1.00f);

        // Capture-group highlight palette. Groups beyond palette size wrap.
        // Not user-customizable yet; hardcoded defaults live here so all
        // draw paths reference a single source of truth.
        std::array<ImVec4, 8> group_colors = {
            ImVec4(0.40f, 0.80f, 1.00f, 1.00f), // 1 cyan
            ImVec4(0.55f, 0.95f, 0.55f, 1.00f), // 2 green
            ImVec4(1.00f, 0.55f, 0.85f, 1.00f), // 3 pink
            ImVec4(0.90f, 0.60f, 0.30f, 1.00f), // 4 orange
            ImVec4(0.75f, 0.65f, 1.00f, 1.00f), // 5 violet
            ImVec4(1.00f, 0.95f, 0.50f, 1.00f), // 6 pale yellow
            ImVec4(0.55f, 1.00f, 0.85f, 1.00f), // 7 mint
            ImVec4(1.00f, 0.70f, 0.70f, 1.00f), // 8 salmon
        };
    };

    struct CursorState
    {
        size_t line = 0;
        size_t offset = 0;
        bool visible = true;
    };

    // Persistent (saved to config).
    struct FilePrefs
    {
        std::vector<std::string> recent_files;
    };

    // Transient load tracking; cleared each session.
    struct FileLoadState
    {
        bool loading = false;
        std::string loading_path;
        std::string loading_error;
        std::optional<std::string> pending_open;
    };

    struct HotkeyTriggers
    {
        bool focus_search = false;
        bool trigger_open_dialog = false;
        bool show_goto_line = false;
        char goto_line_input[32] = {};
    };

    struct Layout
    {
        float search_bar_h = 0.0f;
        float toolbar_h = 0.0f;
        int matches_before = 0;
        int matches_after = 0;
        int visible_line_first = 0;
        int visible_line_last = 0;
        int pending_scroll_line = -1;
        size_t last_cursor_line = (size_t)-1;
        size_t last_cursor_offset = (size_t)-1;
    };

    struct GroupMatch
    {
        std::optional<GroupMatcher> group_matcher_;
    };

    // UI-owned state: user inputs + layout scratch. Derived/view state
    // (matches, source bytes, compile errors) is read live from SearchController.
    struct UiInputs
    {
        bool quit_requested = false;
        bool show_settings = false;

        SearchInputs search;
        ViewPrefs view;
        CursorState cursor;
        FilePrefs file_prefs;
        FileLoadState file_load;
        HotkeyTriggers hotkeys;
        Layout layout;
        GroupMatch groupmatch;
    };

    void draw_ui(UiInputs &inputs, const SearchController &search);

} // namespace kestrel
