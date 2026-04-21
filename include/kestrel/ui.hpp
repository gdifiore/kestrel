#pragma once

#include <imgui.h>
#include <optional>
#include <string>
#include <vector>

namespace kestrel
{

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
        bool display_only_filtered_lines = false;
        bool show_line_nums = true;
        bool snap_scroll = true;
        bool is_dark_mode = true;
        ImVec4 color_match = ImVec4(1.00f, 0.85f, 0.20f, 1.00f);
        ImVec4 color_scope = ImVec4(0.44f, 0.66f, 0.84f, 1.00f);
    };

    struct CursorState
    {
        size_t line = 0;
        size_t offset = 0;
        bool visible = true;
    };

    struct FileState
    {
        bool loading = false;
        std::string loading_path;
        std::string loading_error;
        std::vector<std::string> recent_files;
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
        int matches_before = 0;
        int matches_after = 0;
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
        FileState file;
        HotkeyTriggers hotkeys;
        Layout layout;
    };

    void draw_ui(UiInputs &inputs, const SearchController &search);

} // namespace kestrel
