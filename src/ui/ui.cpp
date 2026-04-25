#include "kestrel/ui.hpp"

#include "kestrel/config.hpp"
#include "kestrel/line_index.hpp"
#include "kestrel/search.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <span>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuiFileDialog.h>

namespace kestrel
{

    static void draw_main_menu(UiInputs &in)
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open...", "Ctrl+O"))
                {
                    IGFD::FileDialogConfig cfg;
                    cfg.path = ".";
                    ImGuiFileDialog::Instance()->OpenDialog(
                        "kestrel_open", "Open file", ".*,.txt,.log,.md", cfg);
                }

                // Recent files submenu
                if (!in.file_prefs.recent_files.empty() && ImGui::BeginMenu("Recent Files"))
                {
                    // Clean up non-existent files first
                    cleanup_recent_files(in);

                    for (size_t i = 0; i < in.file_prefs.recent_files.size(); ++i)
                    {
                        const std::string &path = in.file_prefs.recent_files[i];

                        // Show just filename, full path in tooltip
                        std::filesystem::path file_path(path);
                        std::string display_name = file_path.filename().string();

                        if (ImGui::MenuItem(display_name.c_str()))
                        {
                            in.file_load.pending_open = path;
                        }

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("%s", path.c_str());
                        }
                    }

                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear Recent"))
                    {
                        in.file_prefs.recent_files.clear();
                    }

                    ImGui::EndMenu();
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Ctrl+Q"))
                {
                    in.quit_requested = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                float &scale = ImGui::GetIO().FontGlobalScale;
                if (ImGui::MenuItem("Zoom In", "Ctrl+="))
                    scale = std::min(scale + 0.1f, 3.0f);
                if (ImGui::MenuItem("Zoom Out", "Ctrl+-"))
                    scale = std::max(scale - 0.1f, 0.5f);
                if (ImGui::MenuItem("Reset Zoom", "Ctrl+0"))
                    scale = 1.0f;
                if (ImGui::MenuItem("Dark Mode", nullptr, &in.view.is_dark_mode))
                {
                    if (in.view.is_dark_mode)
                        ImGui::StyleColorsDark();
                    else
                        ImGui::StyleColorsLight();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Handle keyboard shortcuts for file dialog
        if (in.hotkeys.trigger_open_dialog)
        {
            IGFD::FileDialogConfig cfg;
            cfg.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog(
                "kestrel_open", "Open file", ".*,.txt,.log,.md", cfg);
            in.hotkeys.trigger_open_dialog = false;
        }
    }

    static void draw_query_input(UiInputs &in)
    {
        // Handle Ctrl+F focus
        if (in.hotkeys.focus_search)
        {
            ImGui::SetKeyboardFocusHere();
            in.hotkeys.focus_search = false;
        }

        // ImGui InputText reverts buffer on Escape. Snapshot before the call
        // and restore if Escape caused the deactivation, so Esc only unfocuses.
        const bool esc_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
        char query_backup[IM_ARRAYSIZE(in.search.query)];
        if (esc_pressed)
            std::memcpy(query_backup, in.search.query, sizeof(in.search.query));

        ImGui::InputTextWithHint("##query", "search...", in.search.query, IM_ARRAYSIZE(in.search.query));

        if (esc_pressed && ImGui::IsItemDeactivated())
            std::memcpy(in.search.query, query_backup, sizeof(in.search.query));
    }

    static void draw_toolbar(UiInputs &in, const SearchController &search)
    {
        ImGui::Checkbox("Aa", &in.search.case_sensitive);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Case sensitive");

        ImGui::SameLine();
        ImGui::Checkbox(".*", &in.search.dotall);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Dot matches newlines\n(. matches \\n and all characters)");

        ImGui::SameLine();
        ImGui::Checkbox("^$", &in.search.multiline);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Multiline anchors\n(^ and $ match line boundaries)");

        ImGui::SameLine();
        ImGui::Text("%d before", in.layout.matches_before);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Matches before cursor");

        ImGui::SameLine();
        ImGui::Text("%d after", in.layout.matches_after);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Matches after cursor");

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("%zu matches", search.matches().size());
        ImGui::SameLine();
        ImGui::TextDisabled("%.2f ms", search.last_scan_ms());

        ImGui::SameLine();
        ImGui::ColorEdit4("match", &in.view.color_match.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        ImGui::SameLine();
        ImGui::TextUnformatted("match");

        ImGui::SameLine();
        ImGui::ColorEdit4("cursor", &in.view.color_scope.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        ImGui::SameLine();
        ImGui::TextUnformatted("cursor");

        ImGui::SameLine();
        ImGui::Checkbox("line #", &in.view.show_line_nums);

        if (!search.compile_error().empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", search.compile_error().c_str());
        }
    }

    static void format_time_label(int64_t epoch, bool include_date, char *out, size_t cap)
    {
        time_t t = (time_t)epoch;
        struct tm g;
        gmtime_r(&t, &g);
        if (include_date)
            strftime(out, cap, "%Y-%m-%d %H:%M:%S", &g);
        else
            strftime(out, cap, "%H:%M:%S", &g);
    }

    static void draw_time_range(UiInputs &in, const SearchController &search)
    {
        const TimestampIndex &ts = search.timestamp_index();
        if (ts.empty())
            return;
        // Needs ~500px for checkbox + two 200px sliders. Skip if remaining
        // toolbar width can't hold them, so widgets don't wrap/overflow.
        if (ImGui::GetContentRegionAvail().x < 500.0f)
            return;

        auto &tf = in.layout.filters.time;

        // Reset bounds on new file / new timestamp range.
        if (ts.min_ts() != tf.source_min || ts.max_ts() != tf.source_max)
        {
            tf.source_min = ts.min_ts();
            tf.source_max = ts.max_ts();
            tf.start = ts.min_ts();
            tf.end = ts.max_ts();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Checkbox("time", &tf.active);
        if (!tf.active)
            return;

        const bool span_multiday = (ts.max_ts() - ts.min_ts()) >= 24 * 3600;
        char sbuf[32], ebuf[32];
        format_time_label(tf.start, span_multiday, sbuf, sizeof(sbuf));
        format_time_label(tf.end, span_multiday, ebuf, sizeof(ebuf));

        int64_t lo = ts.min_ts();
        int64_t hi = ts.max_ts();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderScalar("##ts_start", ImGuiDataType_S64,
                            &tf.start, &lo, &tf.end, sbuf);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderScalar("##ts_end", ImGuiDataType_S64,
                            &tf.end, &tf.start, &hi, ebuf);
    }

    static void draw_search_bar(UiInputs &in, const SearchController &)
    {
        ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 0));

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##search_bar", nullptr, flags))
        {
            const float gear_w = ImGui::CalcTextSize(" * ").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - gear_w - ImGui::GetStyle().ItemSpacing.x);
            draw_query_input(in);
            ImGui::SameLine();
            if (ImGui::Button(" * "))
                in.show_settings = !in.show_settings;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Settings");
            in.layout.search_bar_h = ImGui::GetWindowHeight();
        }
        ImGui::End();
    }

    static void draw_toolbar_row(UiInputs &in, const SearchController &search)
    {
        ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + in.layout.search_bar_h));
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 0));

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##toolbar", nullptr, flags))
        {
            draw_toolbar(in, search);
            draw_time_range(in, search);
            in.layout.toolbar_h = ImGui::GetWindowHeight();
        }
        ImGui::End();
    }

    static void draw_loading_spinner(const std::string &loading_path, const std::string &loading_error)
    {
        ImGui::Text("Loading file: %s", loading_path.c_str());

        static float spinner_angle = 0.0f;
        spinner_angle += 0.1f;
        if (spinner_angle >= 2.0f * 3.14159f)
            spinner_angle = 0.0f;

        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 center = ImVec2(pos.x + 20, pos.y + 10);
        float radius = 8.0f;
        ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

        for (int i = 0; i < 12; i++)
        {
            float angle = spinner_angle + (i * 2.0f * 3.14159f / 12.0f);
            float alpha = (12 - i) / 12.0f;
            ImU32 fade_color = (color & 0x00FFFFFF) | (IM_COL32_A_MASK & ImU32(alpha * 255) << IM_COL32_A_SHIFT);
            ImVec2 dot_pos = ImVec2(center.x + radius * cosf(angle), center.y + radius * sinf(angle));
            draw_list->AddCircleFilled(dot_pos, 2.0f, fade_color);
        }

        ImGui::Dummy(ImVec2(40, 20)); // Reserve space for spinner

        if (!loading_error.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", loading_error.c_str());
        }
    }

    // Scan the prefix of `line` for a log level keyword. Returns tint color or
    // zero alpha if none. Only first 64 bytes scanned — level keywords
    // conventionally appear near start of the line. Match must be word-bounded
    // (alpha char before/after disqualifies) so identifiers like getError(),
    // /var/log/errors.log, INFORMATIONAL don't tint.
    static ImVec4 detect_log_level_tint(std::span<const char> line)
    {
        size_t n = std::min<size_t>(line.size(), 64);
        auto is_alpha = [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        };
        auto match_at = [&](size_t i, const char *kw) {
            size_t k = 0;
            while (kw[k] && i + k < n &&
                   (line[i + k] == kw[k] || line[i + k] == kw[k] + ('a' - 'A')))
                k++;
            if (kw[k] != '\0')
                return false;
            // Trailing word boundary.
            if (i + k < n && is_alpha(line[i + k]))
                return false;
            return true;
        };
        for (size_t i = 0; i < n; i++)
        {
            // Leading word boundary.
            if (i > 0 && is_alpha(line[i - 1]))
                continue;
            char c = line[i];
            if (c != 'E' && c != 'W' && c != 'I' && c != 'D' && c != 'T' && c != 'F' &&
                c != 'C' &&
                c != 'e' && c != 'w' && c != 'i' && c != 'd' && c != 't' && c != 'f' &&
                c != 'c')
                continue;
            if (match_at(i, "ERROR") || match_at(i, "FATAL") || match_at(i, "CRITICAL"))
                return ImVec4(1.0f, 0.25f, 0.25f, 0.22f);
            if (match_at(i, "WARN") || match_at(i, "WARNING"))
                return ImVec4(1.0f, 0.75f, 0.20f, 0.20f);
            if (match_at(i, "INFO"))
                return ImVec4(0.40f, 0.70f, 1.0f, 0.16f);
            if (match_at(i, "DEBUG"))
                return ImVec4(0.55f, 0.55f, 0.55f, 0.16f);
            if (match_at(i, "TRACE"))
                return ImVec4(0.40f, 0.40f, 0.40f, 0.12f);
        }
        return ImVec4(0, 0, 0, 0);
    }

    static void draw_line_row(UiInputs &in, const SearchController &search,
                              const LineIndex &lines, int total_lines,
                              std::span<const char> source_bytes, int line_idx,
                              float line_height, float char_width)
    {
        if (in.view.show_line_nums)
        {
            if (in.cursor.visible && line_idx == static_cast<int>(in.cursor.line))
            {
                // Highlight cursor line number with cursor color
                ImGui::TextColored(in.view.color_scope, "%7d ", line_idx + 1);
            }
            else
            {
                ImGui::TextDisabled("%7d ", line_idx + 1);
            }
            ImGui::SameLine();
        }
        std::size_t start = lines.line_start(line_idx);
        std::size_t end = (line_idx + 1 < total_lines)
                              ? lines.line_start(line_idx + 1)
                              : source_bytes.size();
        while (end > start && (source_bytes[end - 1] == '\n' || source_bytes[end - 1] == '\r'))
            --end;
        const char *p = source_bytes.data() + start;

        ImVec2 cursor_pos = ImGui::GetCursorScreenPos();

        auto fill_row_tint = [&](ImVec4 col) {
            ImVec2 rmin(cursor_pos.x, cursor_pos.y);
            ImVec2 rmax(cursor_pos.x + ImGui::GetContentRegionAvail().x, cursor_pos.y + line_height);
            ImGui::GetWindowDrawList()->AddRectFilled(rmin, rmax, ImGui::GetColorU32(col));
        };

        if (in.view.log_level_tint)
        {
            ImVec4 tint = detect_log_level_tint(std::span<const char>(p, end - start));
            if (tint.w > 0.0f)
                fill_row_tint(tint);
        }

        if (in.selection.anchor_line)
        {
            size_t a = *in.selection.anchor_line;
            size_t b = in.cursor.line;
            if (a > b) std::swap(a, b);
            if ((size_t)line_idx >= a && (size_t)line_idx <= b)
            {
                ImVec4 sel_col = in.view.color_scope;
                sel_col.w = 0.45f;
                fill_row_tint(sel_col);
            }
        }

        if (in.cursor.visible && line_idx == static_cast<int>(in.cursor.line))
        {
            ImVec4 cur_col = in.view.color_scope;
            cur_col.w = 0.3f;
            fill_row_tint(cur_col);
        }

        std::span matches_in_range = search.matches_in_range(start, end);

        const GroupMatcher *gm = nullptr;
        bool has_groups = false;

        thread_local std::vector<GroupMatcher::Span> group_spans;
        thread_local std::vector<int> group_indices;

        const auto &group_palette = in.view.group_colors;
        int palette_size = 0;

        if (in.view.highlight_groups)
        {
            palette_size = static_cast<int>(group_palette.size());
            gm = in.groupmatch.group_matcher_ ? &*in.groupmatch.group_matcher_ : nullptr;
            has_groups = gm && gm->group_count() > 0;
        }

        // TODO: col_start/col_end are byte offsets. Correct for ASCII;
        // breaks for multibyte UTF-8 (one codepoint spans multiple bytes).
        // Revisit when we care about non-ASCII logs.
        for (auto match : matches_in_range)
        {
            if (match.end <= start || match.start >= end)
                continue;

            std::size_t col_start = std::max(match.start, start) - start;
            std::size_t col_end = std::min(match.end, end) - start;

            ImVec2 p_min(cursor_pos.x + col_start * char_width, cursor_pos.y);
            ImVec2 p_max(cursor_pos.x + col_end * char_width, cursor_pos.y + line_height);
            ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, ImGui::GetColorU32(in.view.color_match));

            if (in.view.highlight_groups && has_groups)
            {
                // Hyperscan with DOTALL can report a single span covering
                // multiple lines (e.g. `^...$` patterns where `.` eats `\n`).
                // SOM_LEFTMOST then pins the start to the earliest line, so
                // PCRE2 anchored at match.start would highlight groups on the
                // wrong line. Detect multi-line matches and anchor PCRE2 to
                // the current line bounds instead.
                const bool single_line = match.start >= start && match.end <= end;
                const std::size_t gm_start = single_line ? match.start : start;
                const std::size_t gm_end = single_line ? match.end : end;
                group_spans.clear();
                group_indices.clear();
                gm->match_into(source_bytes, gm_start, gm_end, group_spans, group_indices);
                for (std::size_t i = 0; i < group_spans.size(); ++i)
                {
                    const auto &g = group_spans[i];

                    if (g.end <= g.start || g.end <= start || g.start >= end)
                        continue;

                    std::size_t g_col_start = std::max(g.start, start) - start;
                    std::size_t g_col_end = std::min(g.end, end) - start;

                    ImVec2 g_min(cursor_pos.x + g_col_start * char_width, cursor_pos.y);
                    ImVec2 g_max(cursor_pos.x + g_col_end * char_width, cursor_pos.y + line_height);
                    ImVec4 col = group_palette[(group_indices[i] - 1) % palette_size];
                    col.w = 0.75f;
                    ImGui::GetWindowDrawList()->AddRectFilled(g_min, g_max, ImGui::GetColorU32(col));
                }
            }
        }

        ImGui::TextUnformatted(p, p + (end - start));

        if (ImGui::IsItemClicked())
        {
            in.selection.extend_or_clear(ImGui::GetIO().KeyShift, in.cursor.line);
            in.cursor.line = static_cast<size_t>(line_idx);
        }
    }

    // Map source-line to display-row in the current view. Returns -1 if not present.
    static int source_to_display_row(const UiInputs &in, const std::vector<size_t> &matched,
                                     bool filter_view, size_t source_line)
    {
        if (in.layout.view_has_custom)
        {
            const auto &v = in.layout.view_lines;
            auto it = std::lower_bound(v.begin(), v.end(), source_line);
            if (it != v.end() && *it == source_line)
                return (int)(it - v.begin());
            return -1;
        }
        if (filter_view)
        {
            auto it = std::lower_bound(matched.begin(), matched.end(), source_line);
            if (it != matched.end() && *it == source_line)
                return (int)(it - matched.begin());
            return -1;
        }
        return (int)source_line;
    }

    static void autoscroll_to_cursor(const UiInputs &in, const std::vector<size_t> &matched,
                                     bool filter_view,
                                     const ImGuiListClipper &clipper)
    {
        int cursor_view_pos = source_to_display_row(in, matched, filter_view, in.cursor.line);

        if (cursor_view_pos >= 0 &&
            (cursor_view_pos < clipper.DisplayStart ||
             cursor_view_pos >= clipper.DisplayEnd))
        {
            float scroll_line_height = ImGui::GetTextLineHeightWithSpacing();
            float target_scroll = cursor_view_pos * scroll_line_height - (ImGui::GetWindowHeight() * 0.5f);
            target_scroll = std::max(0.0f, target_scroll);
            ImGui::SetScrollY(target_scroll);
        }
    }

    // Build/refresh in.layout.view_lines once per frame. Skips work when none
    // of the inputs (time filter range/state, regex filter view, matched set
    // identity, total line count) changed since last frame.
    static void update_view_cache(UiInputs &in, const SearchController &search)
    {
        if (!search.has_source())
        {
            in.layout.view_has_custom = false;
            in.layout.view_lines.clear();
            in.layout.view_cache_matched_ptr = nullptr;
            in.layout.view_cache_matched_size = (size_t)-1;
            in.layout.view_cache_total_lines = -1;
            return;
        }
        const int total_lines = (int)search.line_index().line_count();
        const auto &matched = search.matched_lines();
        const bool filtered = !search.pattern_empty();
        const bool filter_view = in.view.display_only_filtered_lines && filtered;

        // Resolve which filters are active this frame. New filters: append a
        // (state, predicate) pair below + a comparison line in `same`.
        const auto &tf = in.layout.filters.time;
        const bool time_active = tf.active && !search.timestamp_index().empty();
        const bool needs_custom = time_active; // filter_view alone reuses matched directly

        if (!needs_custom)
        {
            in.layout.view_has_custom = false;
            in.layout.view_lines.clear();
            in.layout.view_cache_matched_ptr = nullptr;
            in.layout.view_cache_matched_size = (size_t)-1;
            in.layout.view_cache_total_lines = -1;
            return;
        }

        const bool same =
            in.layout.view_has_custom &&
            in.layout.view_cache_time_filter == time_active &&
            in.layout.view_cache_filter_view == filter_view &&
            in.layout.view_cache_time_start == tf.start &&
            in.layout.view_cache_time_end == tf.end &&
            in.layout.view_cache_matched_ptr == matched.data() &&
            in.layout.view_cache_matched_size == matched.size() &&
            in.layout.view_cache_total_lines == total_lines;
        if (same)
            return;

        const TimestampIndex &ts = search.timestamp_index();
        auto pass = [&](size_t line) {
            // AND of all active filter predicates.
            if (time_active)
            {
                int64_t t = ts.at(line);
                if (t == TimestampIndex::kNone || t < tf.start || t > tf.end)
                    return false;
            }
            return true;
        };
        in.layout.view_lines.clear();
        if (filter_view)
        {
            for (size_t m : matched)
                if (pass(m))
                    in.layout.view_lines.push_back(m);
        }
        else
        {
            for (size_t i = 0; i < (size_t)total_lines; i++)
                if (pass(i))
                    in.layout.view_lines.push_back(i);
        }
        in.layout.view_has_custom = true;
        in.layout.view_cache_time_filter = time_active;
        in.layout.view_cache_filter_view = filter_view;
        in.layout.view_cache_time_start = tf.start;
        in.layout.view_cache_time_end = tf.end;
        in.layout.view_cache_matched_ptr = matched.data();
        in.layout.view_cache_matched_size = matched.size();
        in.layout.view_cache_total_lines = total_lines;
    }

    static void draw_results(UiInputs &in, const SearchController &search)
    {
        ImGuiViewport *vp = ImGui::GetMainViewport();
        float window_pos_y = vp->WorkPos.y + (in.layout.search_bar_h + in.layout.toolbar_h);

        const float results_w = vp->WorkSize.x - (in.view.show_minimap ? MINIMAP_WIDTH : 0);
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, window_pos_y));
        ImGui::SetNextWindowSize(ImVec2(results_w, vp->WorkSize.y - (in.layout.search_bar_h + in.layout.toolbar_h)));

        // NoDecoration would strip the scrollbar too — spell out the flags we want.
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##results", nullptr, flags))
        {
            if (in.layout.pending_scroll_line >= 0)
            {
                float line_h = ImGui::GetTextLineHeightWithSpacing();
                float target = in.layout.pending_scroll_line * line_h - ImGui::GetWindowHeight() * 0.5f;
                ImGui::SetScrollY(std::max(0.0f, target));
                in.layout.pending_scroll_line = -1;
            }

            // Snap scroll to whole lines — prevents half-clipped rows when the
            // trackpad lands on a sub-line offset. Skip near max scroll: flooring
            // there would drop the scroll below max and clip the last line.
            if (in.view.snap_scroll)
            {
                float line_h = ImGui::GetTextLineHeightWithSpacing();
                float scroll_y = ImGui::GetScrollY();
                float max_y = ImGui::GetScrollMaxY();
                float snapped = std::floor(scroll_y / line_h) * line_h;
                if (snapped != scroll_y && scroll_y < max_y - 0.5f)
                    ImGui::SetScrollY(snapped);
            }

            const bool has_source = search.has_source();
            auto source_bytes = has_source ? search.source_bytes() : std::span<const char>{};

            if (in.file_load.loading)
            {
                draw_loading_spinner(in.file_load.loading_path, in.file_load.loading_error);
            }
            else if (!has_source || source_bytes.empty())
            {
                if (!in.file_load.loading_error.empty())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load file: %s", in.file_load.loading_error.c_str());
                }
                else
                {
                    ImGui::TextDisabled("No file loaded. Drop a file or use File > Open...");
                }
            }
            else
            {
                const LineIndex &lines = search.line_index();
                const int total_lines = static_cast<int>(lines.line_count());
                const bool filtered = !search.pattern_empty();
                const auto &matched = search.matched_lines();
                const bool filter_view = in.view.display_only_filtered_lines && filtered;
                const bool use_custom_view = in.layout.view_has_custom;
                const std::vector<size_t> &displayed_lines = in.layout.view_lines;
                const int view_count = use_custom_view
                                           ? (int)displayed_lines.size()
                                       : filter_view
                                           ? static_cast<int>(matched.size())
                                           : total_lines;

                // Average over many glyphs: per-glyph rounding in CalcTextSize
                // accumulates otherwise, and match rects drift off by ~1 char per ~40.
                float char_width = ImGui::CalcTextSize(
                                       "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM")
                                       .x /
                                   50.0f;
                float line_height = ImGui::GetTextLineHeight();

                if (filtered && matched.empty() && search.compile_error().empty())
                {
                    ImGui::TextDisabled("No matches.");
                }

                ImGuiListClipper clipper;
                clipper.Begin(view_count);
                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    {
                        const int line_idx = use_custom_view ? (int)displayed_lines[i]
                                             : filter_view   ? static_cast<int>(matched[i])
                                                             : i;
                        draw_line_row(in, search, lines, total_lines, source_bytes,
                                      line_idx, line_height, char_width);
                    }
                }

                if (in.cursor.visible && has_source &&
                    (in.cursor.line != in.layout.last_cursor_line ||
                     in.cursor.offset != in.layout.last_cursor_offset))
                {
                    autoscroll_to_cursor(in, matched, filter_view, clipper);
                    in.layout.last_cursor_line = in.cursor.line;
                    in.layout.last_cursor_offset = in.cursor.offset;
                }
            }
            float line_h = ImGui::GetTextLineHeightWithSpacing();
            in.layout.visible_line_first = (int)(ImGui::GetScrollY() / line_h);
            in.layout.visible_line_last = in.layout.visible_line_first + (int)(ImGui::GetWindowHeight() / line_h);
        }

        ImGui::End();
    }

    static void draw_minimap(UiInputs &in, const SearchController &search)
    {
        if (!in.view.show_minimap)
            return;
        ImGuiViewport *vp = ImGui::GetMainViewport();
        float window_pos_x = vp->WorkPos.x + vp->WorkSize.x - MINIMAP_WIDTH;
        float window_pos_y = vp->WorkPos.y + (in.layout.search_bar_h + in.layout.toolbar_h);
        int window_height_px = (int)(vp->WorkSize.y - (in.layout.search_bar_h + in.layout.toolbar_h));

        const std::vector<size_t> &orig_matches = search.matched_lines();
        const bool pattern_active = !search.pattern_empty();
        const bool filtered = in.view.display_only_filtered_lines && pattern_active;
        const bool use_custom_view = in.layout.view_has_custom;
        const std::vector<size_t> &view_lines = in.layout.view_lines;
        int line_count;
        if (use_custom_view)
            line_count = (int)view_lines.size();
        else if (filtered)
            line_count = (int)orig_matches.size();
        else
            line_count = (int)search.line_index().line_count();

        // cursor's row index in the displayed coordinate space (not source line).
        int cursor_row = source_to_display_row(in, orig_matches, filtered, in.cursor.line);

        ImGui::SetNextWindowPos(ImVec2(window_pos_x, window_pos_y));
        ImGui::SetNextWindowSize(ImVec2(MINIMAP_WIDTH, (float)window_height_px));

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##minimap", nullptr, flags))
        {
            ImDrawList *dl = ImGui::GetWindowDrawList();

            // Background fill for contrast.
            dl->AddRectFilled(
                ImVec2(window_pos_x, window_pos_y),
                ImVec2(window_pos_x + MINIMAP_WIDTH, window_pos_y + window_height_px),
                ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.15f)));

            ImVec2 btn_min = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##minimap_hit", ImVec2(MINIMAP_WIDTH, (float)window_height_px));
            if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f)
            {
                if (ImGuiWindow *w = ImGui::FindWindowByName("##results"))
                {
                    float line_h = ImGui::GetTextLineHeightWithSpacing();
                    ImGui::SetScrollY(w, w->Scroll.y - ImGui::GetIO().MouseWheel * line_h * 3.0f);
                }
            }

            if (line_count > 0)
            {
                // Target line in display-row space under the mouse.
                float mouse_y = ImGui::GetIO().MousePos.y - btn_min.y;
                int target = (int)((mouse_y / (float)window_height_px) * line_count);
                target = std::clamp(target, 0, line_count - 1);
                const size_t src_line_for_target = use_custom_view ? view_lines[target]
                                                   : filtered      ? orig_matches[target]
                                                                   : (size_t)target;

                if (ImGui::IsItemActive())
                {
                    in.layout.pending_scroll_line = target;
                    in.cursor.line = src_line_for_target;
                    in.cursor.offset = 0;
                    // Suppress autoscroll-to-cursor next frame; pending_scroll_line
                    // already positions the view, and autoscroll would just redo it.
                    in.layout.last_cursor_line = in.cursor.line;
                    in.layout.last_cursor_offset = in.cursor.offset;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("line %zu", src_line_for_target + 1);

                // Row size: cap small-file rows to 4px; large files scale to fit exactly.
                float row_size_f;
                if (line_count < window_height_px)
                {
                    const float MAX_ROW_PX = 4.0f;
                    row_size_f = std::min((float)window_height_px / (float)line_count, MAX_ROW_PX);
                }
                else
                {
                    row_size_f = (float)window_height_px / (float)line_count;
                }
                float mark_h = std::max(row_size_f, 1.0f);

                // Match marks — iterate matches only (O(m), not O(n)). Under
                // custom view, map each match's source line to its display row
                // via binary search; matches outside the view are skipped.
                ImU32 match_col = ImGui::GetColorU32(ImVec4(
                    in.view.color_match.x, in.view.color_match.y, in.view.color_match.z, 0.6f));
                for (size_t k = 0; k < orig_matches.size(); k++)
                {
                    int row;
                    if (use_custom_view)
                    {
                        auto it = std::lower_bound(view_lines.begin(), view_lines.end(), orig_matches[k]);
                        if (it == view_lines.end() || *it != orig_matches[k])
                            continue;
                        row = (int)(it - view_lines.begin());
                    }
                    else
                    {
                        row = filtered ? (int)k : (int)orig_matches[k];
                    }
                    if (row >= line_count)
                        break;
                    float y = window_pos_y + row * row_size_f;
                    dl->AddRectFilled(
                        ImVec2(window_pos_x, y),
                        ImVec2(window_pos_x + MINIMAP_WIDTH, y + mark_h),
                        match_col);
                }

                // Viewport indicator.
                if ((in.layout.visible_line_last - in.layout.visible_line_first) < line_count)
                {
                    const float MIN_VIEWPORT_H = 30.0f;
                    float vp_h = (in.layout.visible_line_last - in.layout.visible_line_first) * row_size_f;
                    vp_h = std::max(vp_h, MIN_VIEWPORT_H);
                    float y = window_pos_y + in.layout.visible_line_first * row_size_f;
                    float max_y = window_pos_y + window_height_px - vp_h;
                    y = std::min(y, max_y);
                    dl->AddRectFilled(
                        ImVec2(window_pos_x, y),
                        ImVec2(window_pos_x + MINIMAP_WIDTH, y + vp_h),
                        ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 0.25f)));
                }

                // Cursor line — drawn last so it sits on top.
                if (cursor_row >= 0 && cursor_row < line_count)
                {
                    float cy = window_pos_y + cursor_row * row_size_f;
                    dl->AddRectFilled(
                        ImVec2(window_pos_x, cy),
                        ImVec2(window_pos_x + MINIMAP_WIDTH, cy + mark_h),
                        ImGui::GetColorU32(in.view.color_scope));
                }
            }
        }
        ImGui::End();
    }

    static void draw_shortcut_row(const char *key, const char *action)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "%s", key);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(action);
    }

    static void draw_shortcuts_table()
    {
        if (!ImGui::BeginTable("shortcuts", 2, ImGuiTableFlags_SizingFixedFit))
            return;
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);

        draw_shortcut_row("Ctrl+F", "Focus search box");
        draw_shortcut_row("Ctrl+O", "Open file dialog");
        draw_shortcut_row("Ctrl+Q", "Quit application");
        draw_shortcut_row("Escape", "Unfocus input");
        draw_shortcut_row("Ctrl+L", "Clear search");
        draw_shortcut_row("n", "Next match");
        draw_shortcut_row("Shift+N", "Previous match");
        draw_shortcut_row("↑↓ PgUp/Dn", "Navigate lines");
        draw_shortcut_row("Home/End", "First/last line");
        draw_shortcut_row("Ctrl+G", "Go to line");
        draw_shortcut_row("Shift+Click", "Extend line selection");
        draw_shortcut_row("Shift+↑↓/PgUp/Dn", "Extend line selection");
        draw_shortcut_row("Ctrl+C", "Copy selection (or search pattern)");
        draw_shortcut_row("Ctrl+V", "Paste to search");

        ImGui::EndTable();
    }

    static void draw_settings_popup(UiInputs &in)
    {
        if (!in.show_settings)
            return;
        if (ImGui::Begin("Settings", &in.show_settings, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::SeparatorText("Display");
            ImGui::Checkbox("Show minimap", &in.view.show_minimap);
            ImGui::Checkbox("Tint by log level", &in.view.log_level_tint);
            ImGui::Checkbox("Snap scroll to lines", &in.view.snap_scroll);
            ImGui::Checkbox("Show only filtered results", &in.view.display_only_filtered_lines);
            ImGui::Checkbox("Color each regex capture group individually", &in.view.highlight_groups);

            ImGui::SeparatorText("Regex Flags");
            ImGui::Checkbox("Case sensitive", &in.search.case_sensitive);

            ImGui::Checkbox("Dot matches newlines", &in.search.dotall);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Make . (dot) match newline characters\nPattern 'foo.*bar' can match across lines");

            ImGui::Checkbox("Multiline anchors", &in.search.multiline);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Make ^ and $ match line boundaries\n^ = start of line, $ = end of line");

            ImGui::SeparatorText("Keyboard Shortcuts");
            draw_shortcuts_table();
        }
        ImGui::End();
    }

    static void draw_open_dialog(UiInputs &in)
    {
        auto *dlg = ImGuiFileDialog::Instance();
        ImVec2 min_size(600, 400);
        if (dlg->Display("kestrel_open", ImGuiWindowFlags_NoCollapse, min_size))
        {
            if (dlg->IsOk())
            {
                in.file_load.pending_open = dlg->GetFilePathName();
            }
            dlg->Close();
        }
    }

    static void draw_goto_line_dialog(UiInputs &in, const SearchController &search)
    {
        if (!in.hotkeys.show_goto_line)
            return;

        if (ImGui::Begin("Go to Line", &in.hotkeys.show_goto_line, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
        {
            ImGui::Text("Enter line number:");
            ImGui::SetNextItemWidth(200.0f);

            bool enter_pressed = ImGui::InputText("##line", in.hotkeys.goto_line_input, sizeof(in.hotkeys.goto_line_input), ImGuiInputTextFlags_EnterReturnsTrue);

            if (ImGui::IsWindowAppearing())
            {
                ImGui::SetKeyboardFocusHere(-1); // Focus the input field
            }

            ImGui::Spacing();

            bool go_button = ImGui::Button("Go");
            ImGui::SameLine();
            bool cancel_button = ImGui::Button("Cancel");

            if (enter_pressed || go_button)
            {
                // Parse line number and jump to it
                char *endptr;
                long line_num = strtol(in.hotkeys.goto_line_input, &endptr, 10);

                if (*endptr == '\0' && line_num > 0 && search.has_source())
                {
                    // Convert to 0-based and clamp to valid range
                    size_t target_line = static_cast<size_t>(line_num - 1);
                    size_t max_line = search.line_index().line_count();
                    if (max_line > 0)
                    {
                        target_line = std::min(target_line, max_line - 1);
                        in.cursor.line = target_line;
                        in.cursor.offset = search.line_index().line_start(target_line);
                    }
                }

                // Close dialog and clear input
                in.hotkeys.show_goto_line = false;
                in.hotkeys.goto_line_input[0] = '\0';
            }

            if (cancel_button)
            {
                in.hotkeys.show_goto_line = false;
                in.hotkeys.goto_line_input[0] = '\0';
            }
        }
        ImGui::End();
    }

    void draw_ui(UiInputs &in, const SearchController &search)
    {
        draw_main_menu(in);
        draw_toolbar_row(in, search);
        draw_search_bar(in, search);
        update_view_cache(in, search);
        draw_results(in, search);
        draw_minimap(in, search);
        draw_settings_popup(in);
        draw_open_dialog(in);
        draw_goto_line_dialog(in, search);
    }

} // namespace kestrel
