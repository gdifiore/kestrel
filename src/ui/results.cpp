#include "ui_internal.hpp"

#include "kestrel/line_index.hpp"
#include "kestrel/search.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

namespace kestrel
{

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

    int source_to_display_row(const UiInputs &in, const std::vector<size_t> &matched,
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
    void update_view_cache(UiInputs &in, const SearchController &search)
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

    void draw_results(UiInputs &in, const SearchController &search)
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

} // namespace kestrel
