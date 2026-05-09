#pragma once

#include "kestrel/group_matcher.hpp"

#include <imgui.h>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kestrel
{
    // Reference font size all hardcoded pixel constants are tuned for.
    // Window::ctor loads the TTF at BASE_FONT_PX * derive_ui_scale().
    inline constexpr float BASE_FONT_PX = 15.0F;

    // Base width at BASE_FONT_PX; scale at use sites via ui_scale().
    inline constexpr int MINIMAP_WIDTH = 80; // px

    // Multiplier vs the base font size that the rest of the UI is sized for.
    // Tracks both the startup DPI scale baked into the font and any runtime
    // FontGlobalScale zoom, so hardcoded pixel constants stay proportional.
    inline float ui_scale()
    {
        return ImGui::GetFontSize() / BASE_FONT_PX;
    }

    inline float minimap_width()
    {
        return MINIMAP_WIDTH * ui_scale();
    }

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

    // Line-range selection. When set, selection covers
    // [min(anchor,cursor), max(anchor,cursor)] inclusive.
    struct SelectionState
    {
        std::optional<size_t> anchor_line;

        void extend_or_clear(bool shift, size_t cur_line)
        {
            if (!shift)
                anchor_line.reset();
            else if (!anchor_line)
                anchor_line = cur_line;
        }
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
        // Path of currently loaded file (set on successful load).
        std::string current_path;
    };

    struct HotkeyTriggers
    {
        bool focus_search = false;
        bool trigger_open_dialog = false;
        bool show_goto_line = false;
        char goto_line_input[32] = {};
    };

    // Per-frame display-view filters. Each filter is independent; the displayed
    // line set is the intersection of every active filter (∩ regex matches when
    // display_only_filtered_lines is on). Add new filters here and extend
    // update_view_cache() to AND them into the predicate.
    struct ViewFilters
    {
        // Time-range filter — keep lines whose parsed timestamp is in [start, end].
        struct Time
        {
            bool active = false;
            int64_t start = 0;
            int64_t end = 0;
            // Snapshot of TimestampIndex bounds; on change, slider bounds are
            // reset to the new [min, max].
            int64_t source_min = 0;
            int64_t source_max = 0;
        } time;
    };

    struct Layout
    {
        float search_bar_h = 0.0f;
        float toolbar_h = 0.0f;
        float status_bar_h = 0.0f;
        ViewFilters filters;
        int matches_before = 0;
        int matches_after = 0;
        int visible_line_first = 0;
        int visible_line_last = 0;
        int pending_scroll_line = -1;
        size_t last_cursor_line = (size_t)-1;
        size_t last_cursor_offset = (size_t)-1;

        // Display view: when view_has_custom, view_lines holds the source-line
        // indices to render in row order (regex-filter ∩ time-filter).
        // Otherwise the view is identity (row i → line i).
        std::vector<size_t> view_lines;
        bool view_has_custom = false;
        // Cache keys for view_lines — invalidate when any input changes.
        // completed_gen folds matched-set + total-lines identity into one
        // counter bumped by SearchController on every scan/load completion.
        uint64_t view_cache_completed_gen = 0;
        int64_t view_cache_time_start = 0;
        int64_t view_cache_time_end = 0;
        bool view_cache_time_filter = false;
        bool view_cache_filter_view = false;
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
        SelectionState selection;
        FilePrefs file_prefs;
        FileLoadState file_load;
        HotkeyTriggers hotkeys;
        Layout layout;
        GroupMatch groupmatch;
    };

    void draw_ui(UiInputs &inputs, const SearchController &search);

    // Maps display rows ↔ source lines for the current frame's view. The view
    // is one of: full source (identity), regex-filtered (matched lines only),
    // or custom (regex ∩ time-filter, materialized in layout.view_lines).
    // Centralizes the switch so cursor nav, autoscroll, and minimap agree.
    struct ViewIndex
    {
        const std::vector<size_t> &view_lines; // active when use_custom
        const std::vector<size_t> &matched;    // active when filter_view && !use_custom
        int total_lines;
        bool use_custom;
        bool filter_view;

        int row_count() const noexcept;
        size_t row_to_source(int row) const;     // row clamped to [0, row_count)
        int source_to_row(size_t source_line) const; // -1 if filtered out
    };

    ViewIndex make_view_index(const UiInputs &in, const SearchController &search);

} // namespace kestrel
