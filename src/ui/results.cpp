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
    namespace
    {
        constexpr float TWO_PI = 2.0F * 3.14159F;
        constexpr int SPINNER_DOTS = 12;

        void draw_loading_spinner(const std::string &loading_path, const std::string &loading_error)
        {
            ImGui::Text("Loading file: %s", loading_path.c_str());

            static float spinner_angle = 0.0F;
            spinner_angle += 0.1F;
            if (spinner_angle >= TWO_PI)
            {
                spinner_angle = 0.0F;
            }

            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImVec2 center = ImVec2(pos.x + 20, pos.y + 10);
            float radius = 8.0F;
            ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

            for (int i = 0; i < SPINNER_DOTS; i++)
            {
                float angle = spinner_angle + (i * TWO_PI / static_cast<float>(SPINNER_DOTS));
                float alpha = static_cast<float>(SPINNER_DOTS - i) / static_cast<float>(SPINNER_DOTS);
                ImU32 fade_color = (color & 0x00FFFFFF) |
                                   (IM_COL32_A_MASK & ImU32(alpha * 255) << IM_COL32_A_SHIFT);
                ImVec2 dot_pos = ImVec2(center.x + radius * cosf(angle),
                                        center.y + radius * sinf(angle));
                draw_list->AddCircleFilled(dot_pos, 2.0F, fade_color);
            }

            ImGui::Dummy(ImVec2(40, 20));

            if (!loading_error.empty())
            {
                ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.4F, 1.0F), "Error: %s", loading_error.c_str());
            }
        }

        bool is_alpha_byte(char c)
        {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        }

        bool keyword_matches_at(std::span<const char> line, size_t pos, const char *kw, size_t end)
        {
            size_t k = 0;
            while (kw[k] != '\0' && pos + k < end &&
                   (line[pos + k] == kw[k] || line[pos + k] == kw[k] + ('a' - 'A')))
            {
                k++;
            }
            if (kw[k] != '\0')
            {
                return false;
            }
            // Trailing word boundary.
            if (pos + k < end && is_alpha_byte(line[pos + k]))
            {
                return false;
            }
            return true;
        }

        bool is_level_first_char(char c)
        {
            switch (c)
            {
            case 'E':
            case 'W':
            case 'I':
            case 'D':
            case 'T':
            case 'F':
            case 'C':
            case 'e':
            case 'w':
            case 'i':
            case 'd':
            case 't':
            case 'f':
            case 'c':
                return true;
            default:
                return false;
            }
        }

        ImVec4 level_tint_at(std::span<const char> line, size_t pos, size_t end)
        {
            if (keyword_matches_at(line, pos, "ERROR", end) ||
                keyword_matches_at(line, pos, "FATAL", end) ||
                keyword_matches_at(line, pos, "CRITICAL", end))
            {
                return ImVec4(1.0F, 0.25F, 0.25F, 0.22F);
            }
            if (keyword_matches_at(line, pos, "WARN", end) ||
                keyword_matches_at(line, pos, "WARNING", end))
            {
                return ImVec4(1.0F, 0.75F, 0.20F, 0.20F);
            }
            if (keyword_matches_at(line, pos, "INFO", end))
            {
                return ImVec4(0.40F, 0.70F, 1.0F, 0.16F);
            }
            if (keyword_matches_at(line, pos, "DEBUG", end))
            {
                return ImVec4(0.55F, 0.55F, 0.55F, 0.16F);
            }
            if (keyword_matches_at(line, pos, "TRACE", end))
            {
                return ImVec4(0.40F, 0.40F, 0.40F, 0.12F);
            }
            return ImVec4(0, 0, 0, 0);
        }

        // Scan the prefix of `line` for a log level keyword. Returns tint color or
        // zero alpha if none. Only first 64 bytes scanned — level keywords
        // conventionally appear near start of the line. Match must be word-bounded
        // (alpha char before/after disqualifies) so identifiers like getError(),
        // /var/log/errors.log, INFORMATIONAL don't tint.
        ImVec4 detect_log_level_tint(std::span<const char> line)
        {
            const size_t n = std::min<size_t>(line.size(), 64);
            for (size_t i = 0; i < n; i++)
            {
                if (i > 0 && is_alpha_byte(line[i - 1]))
                {
                    continue;
                }
                if (!is_level_first_char(line[i]))
                {
                    continue;
                }
                ImVec4 tint = level_tint_at(line, i, n);
                if (tint.w > 0.0F)
                {
                    return tint;
                }
            }
            return ImVec4(0, 0, 0, 0);
        }

        void fill_row_tint(ImVec2 cursor_pos, float line_height, ImVec4 col)
        {
            ImVec2 rmin(cursor_pos.x, cursor_pos.y);
            ImVec2 rmax(cursor_pos.x + ImGui::GetContentRegionAvail().x, cursor_pos.y + line_height);
            ImGui::GetWindowDrawList()->AddRectFilled(rmin, rmax, ImGui::GetColorU32(col));
        }

        void draw_row_tints(const UiInputs &in, int line_idx, std::span<const char> line_view,
                            ImVec2 cursor_pos, float line_height)
        {
            if (in.view.log_level_tint)
            {
                ImVec4 tint = detect_log_level_tint(line_view);
                if (tint.w > 0.0F)
                {
                    fill_row_tint(cursor_pos, line_height, tint);
                }
            }

            if (in.selection.anchor_line)
            {
                size_t a = *in.selection.anchor_line;
                size_t b = in.cursor.line;
                if (a > b)
                {
                    std::swap(a, b);
                }
                if (static_cast<size_t>(line_idx) >= a && static_cast<size_t>(line_idx) <= b)
                {
                    ImVec4 sel_col = in.view.color_scope;
                    sel_col.w = 0.45F;
                    fill_row_tint(cursor_pos, line_height, sel_col);
                }
            }

            if (in.cursor.visible && line_idx == static_cast<int>(in.cursor.line))
            {
                ImVec4 cur_col = in.view.color_scope;
                cur_col.w = 0.3F;
                fill_row_tint(cursor_pos, line_height, cur_col);
            }
        }

        struct LineRowSpan
        {
            std::size_t start;
            std::size_t end;
        };

        void draw_group_highlights(const UiInputs &in, const GroupMatcher &gm,
                                   std::span<const char> source_bytes, LineRowSpan row,
                                   const Match &match,
                                   ImVec2 cursor_pos, float line_height, float char_width,
                                   int palette_size)
        {
            // Hyperscan with DOTALL can report a single span covering
            // multiple lines (e.g. `^...$` patterns where `.` eats `\n`).
            // SOM_LEFTMOST then pins the start to the earliest line, so
            // PCRE2 anchored at match.start would highlight groups on the
            // wrong line. Detect multi-line matches and anchor PCRE2 to
            // the current line bounds instead.
            const bool single_line = match.start >= row.start && match.end <= row.end;
            const std::size_t gm_start = single_line ? match.start : row.start;
            const std::size_t gm_end = single_line ? match.end : row.end;

            thread_local std::vector<GroupMatcher::Span> group_spans;
            thread_local std::vector<int> group_indices;
            group_spans.clear();
            group_indices.clear();
            gm.match_into(source_bytes, gm_start, gm_end, group_spans, group_indices);

            const auto &group_palette = in.view.group_colors;
            for (std::size_t i = 0; i < group_spans.size(); ++i)
            {
                const auto &g = group_spans[i];
                if (g.end <= g.start || g.end <= row.start || g.start >= row.end)
                {
                    continue;
                }
                std::size_t g_col_start = std::max(g.start, row.start) - row.start;
                std::size_t g_col_end = std::min(g.end, row.end) - row.start;

                ImVec2 g_min(cursor_pos.x + g_col_start * char_width, cursor_pos.y);
                ImVec2 g_max(cursor_pos.x + g_col_end * char_width, cursor_pos.y + line_height);
                ImVec4 col = group_palette[(group_indices[i] - 1) % palette_size];
                col.w = 0.75F;
                ImGui::GetWindowDrawList()->AddRectFilled(g_min, g_max, ImGui::GetColorU32(col));
            }
        }

        void draw_match_highlights(const UiInputs &in, const SearchController &search,
                                   std::span<const char> source_bytes, LineRowSpan row,
                                   ImVec2 cursor_pos, float line_height, float char_width)
        {
            std::span matches_in_range = search.matches_in_range(row.start, row.end);

            const GroupMatcher *gm = nullptr;
            int palette_size = 0;
            const bool highlight_groups = in.view.highlight_groups;
            if (highlight_groups)
            {
                palette_size = static_cast<int>(in.view.group_colors.size());
                gm = in.groupmatch.group_matcher_ ? &*in.groupmatch.group_matcher_ : nullptr;
            }
            const bool has_groups = gm != nullptr && gm->group_count() > 0;

            // TODO: col_start/col_end are byte offsets. Correct for ASCII;
            // breaks for multibyte UTF-8 (one codepoint spans multiple bytes).
            // Revisit when we care about non-ASCII logs.
            for (auto match : matches_in_range)
            {
                if (match.end <= row.start || match.start >= row.end)
                {
                    continue;
                }

                std::size_t col_start = std::max(match.start, row.start) - row.start;
                std::size_t col_end = std::min(match.end, row.end) - row.start;

                ImVec2 p_min(cursor_pos.x + col_start * char_width, cursor_pos.y);
                ImVec2 p_max(cursor_pos.x + col_end * char_width, cursor_pos.y + line_height);
                ImGui::GetWindowDrawList()->AddRectFilled(
                    p_min, p_max, ImGui::GetColorU32(in.view.color_match));

                if (highlight_groups && has_groups)
                {
                    draw_group_highlights(in, *gm, source_bytes, row, match,
                                          cursor_pos, line_height, char_width, palette_size);
                }
            }
        }

        void draw_line_number(const UiInputs &in, int line_idx)
        {
            if (!in.view.show_line_nums)
            {
                return;
            }
            if (in.cursor.visible && line_idx == static_cast<int>(in.cursor.line))
            {
                ImGui::TextColored(in.view.color_scope, "%7d ", line_idx + 1);
            }
            else
            {
                ImGui::TextDisabled("%7d ", line_idx + 1);
            }
            ImGui::SameLine();
        }

        LineRowSpan compute_line_row(const LineIndex &lines, int total_lines,
                                     std::span<const char> source_bytes, int line_idx)
        {
            std::size_t start = lines.line_start(line_idx);
            std::size_t end = (line_idx + 1 < total_lines)
                                  ? lines.line_start(line_idx + 1)
                                  : source_bytes.size();
            while (end > start && (source_bytes[end - 1] == '\n' || source_bytes[end - 1] == '\r'))
            {
                --end;
            }
            return {start, end};
        }

        void draw_line_row(UiInputs &in, const SearchController &search,
                           const LineIndex &lines, int total_lines,
                           std::span<const char> source_bytes, int line_idx,
                           float line_height, float char_width)
        {
            draw_line_number(in, line_idx);

            const LineRowSpan row = compute_line_row(lines, total_lines, source_bytes, line_idx);
            const char *p = source_bytes.data() + row.start;
            const ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
            const std::span<const char> line_view(p, row.end - row.start);

            draw_row_tints(in, line_idx, line_view, cursor_pos, line_height);
            draw_match_highlights(in, search, source_bytes, row, cursor_pos, line_height, char_width);

            ImGui::TextUnformatted(p, p + (row.end - row.start));

            if (ImGui::IsItemClicked())
            {
                in.selection.extend_or_clear(ImGui::GetIO().KeyShift, in.cursor.line);
                in.cursor.line = static_cast<size_t>(line_idx);
            }
        }

        void autoscroll_to_cursor(const UiInputs &in, const std::vector<size_t> &matched,
                                  bool filter_view, const ImGuiListClipper &clipper)
        {
            int cursor_view_pos = source_to_display_row(in, matched, filter_view, in.cursor.line);

            if (cursor_view_pos >= 0 &&
                (cursor_view_pos < clipper.DisplayStart ||
                 cursor_view_pos >= clipper.DisplayEnd))
            {
                float scroll_line_height = ImGui::GetTextLineHeightWithSpacing();
                float target_scroll = cursor_view_pos * scroll_line_height -
                                      (ImGui::GetWindowHeight() * 0.5F);
                target_scroll = std::max(0.0F, target_scroll);
                ImGui::SetScrollY(target_scroll);
            }
        }

        void apply_pending_scroll(UiInputs &in)
        {
            if (in.layout.pending_scroll_line < 0)
            {
                return;
            }
            float line_h = ImGui::GetTextLineHeightWithSpacing();
            float target = in.layout.pending_scroll_line * line_h - ImGui::GetWindowHeight() * 0.5F;
            ImGui::SetScrollY(std::max(0.0F, target));
            in.layout.pending_scroll_line = -1;
        }

        // Snap scroll to whole lines — prevents half-clipped rows when the
        // trackpad lands on a sub-line offset. Skip near max scroll: flooring
        // there would drop the scroll below max and clip the last line.
        void snap_scroll_to_line(const UiInputs &in)
        {
            if (!in.view.snap_scroll)
            {
                return;
            }
            float line_h = ImGui::GetTextLineHeightWithSpacing();
            float scroll_y = ImGui::GetScrollY();
            float max_y = ImGui::GetScrollMaxY();
            float snapped = std::floor(scroll_y / line_h) * line_h;
            if (snapped != scroll_y && scroll_y < max_y - 0.5F)
            {
                ImGui::SetScrollY(snapped);
            }
        }

        void draw_empty_state(const UiInputs &in)
        {
            if (!in.file_load.loading_error.empty())
            {
                ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.4F, 1.0F),
                                   "Failed to load file: %s",
                                   in.file_load.loading_error.c_str());
            }
            else
            {
                ImGui::TextDisabled("No file loaded. Drop a file or use File > Open...");
            }
        }

        struct ResultsView
        {
            const std::vector<size_t> &matched;
            const std::vector<size_t> &displayed_lines;
            int total_lines;
            int view_count;
            bool filter_view;
            bool use_custom_view;
        };

        ResultsView build_view(const UiInputs &in, const SearchController &search)
        {
            const auto &matched = search.matched_lines();
            const auto &displayed = in.layout.view_lines;
            const int total_lines = static_cast<int>(search.line_index().line_count());
            const bool filtered = !search.pattern_empty();
            const bool filter_view = in.view.display_only_filtered_lines && filtered;
            const bool use_custom_view = in.layout.view_has_custom;
            const int view_count = use_custom_view
                                       ? static_cast<int>(displayed.size())
                                   : filter_view
                                       ? static_cast<int>(matched.size())
                                       : total_lines;
            return {matched, displayed, total_lines, view_count, filter_view, use_custom_view};
        }

        float compute_char_width()
        {
            // Average over many glyphs: per-glyph rounding in CalcTextSize
            // accumulates otherwise, and match rects drift off by ~1 char per ~40.
            return ImGui::CalcTextSize(
                       "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM")
                       .x /
                   50.0F;
        }

        void draw_results_body(UiInputs &in, const SearchController &search,
                               std::span<const char> source_bytes)
        {
            const ResultsView view = build_view(in, search);
            const LineIndex &lines = search.line_index();
            const float char_width = compute_char_width();
            const float line_height = ImGui::GetTextLineHeight();

            if (!search.pattern_empty() && view.matched.empty() && search.compile_error().empty())
            {
                ImGui::TextDisabled("No matches.");
            }

            ImGuiListClipper clipper;
            clipper.Begin(view.view_count);
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    const int line_idx = view.use_custom_view ? static_cast<int>(view.displayed_lines[i])
                                         : view.filter_view   ? static_cast<int>(view.matched[i])
                                                              : i;
                    draw_line_row(in, search, lines, view.total_lines, source_bytes,
                                  line_idx, line_height, char_width);
                }
            }

            if (in.cursor.visible &&
                (in.cursor.line != in.layout.last_cursor_line ||
                 in.cursor.offset != in.layout.last_cursor_offset))
            {
                autoscroll_to_cursor(in, view.matched, view.filter_view, clipper);
                in.layout.last_cursor_line = in.cursor.line;
                in.layout.last_cursor_offset = in.cursor.offset;
            }
        }

        void update_visible_range(UiInputs &in)
        {
            float line_h = ImGui::GetTextLineHeightWithSpacing();
            in.layout.visible_line_first = static_cast<int>(ImGui::GetScrollY() / line_h);
            in.layout.visible_line_last = in.layout.visible_line_first +
                                          static_cast<int>(ImGui::GetWindowHeight() / line_h);
        }
    } // namespace

    int source_to_display_row(const UiInputs &in, const std::vector<size_t> &matched,
                              bool filter_view, size_t source_line)
    {
        if (in.layout.view_has_custom)
        {
            const auto &v = in.layout.view_lines;
            auto it = std::lower_bound(v.begin(), v.end(), source_line);
            if (it != v.end() && *it == source_line)
            {
                return static_cast<int>(it - v.begin());
            }
            return -1;
        }
        if (filter_view)
        {
            auto it = std::lower_bound(matched.begin(), matched.end(), source_line);
            if (it != matched.end() && *it == source_line)
            {
                return static_cast<int>(it - matched.begin());
            }
            return -1;
        }
        return static_cast<int>(source_line);
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
            in.layout.view_cache_matched_size = static_cast<size_t>(-1);
            in.layout.view_cache_total_lines = -1;
            return;
        }
        const int total_lines = static_cast<int>(search.line_index().line_count());
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
            in.layout.view_cache_matched_size = static_cast<size_t>(-1);
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
        {
            return;
        }

        const TimestampIndex &ts = search.timestamp_index();
        auto pass = [&](size_t line)
        {
            // AND of all active filter predicates.
            if (time_active)
            {
                int64_t t = ts.at(line);
                if (t == TimestampIndex::kNone || t < tf.start || t > tf.end)
                {
                    return false;
                }
            }
            return true;
        };
        in.layout.view_lines.clear();
        if (filter_view)
        {
            for (size_t m : matched)
            {
                if (pass(m))
                {
                    in.layout.view_lines.push_back(m);
                }
            }
        }
        else
        {
            for (size_t i = 0; i < static_cast<size_t>(total_lines); i++)
            {
                if (pass(i))
                {
                    in.layout.view_lines.push_back(i);
                }
            }
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
        const float top_h = in.layout.search_bar_h + in.layout.toolbar_h;
        const float window_pos_y = vp->WorkPos.y + top_h;
        const float results_w = vp->WorkSize.x - (in.view.show_minimap ? MINIMAP_WIDTH : 0);

        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, window_pos_y));
        ImGui::SetNextWindowSize(ImVec2(results_w, vp->WorkSize.y - top_h - in.layout.status_bar_h));

        // NoDecoration would strip the scrollbar too — spell out the flags we want.
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##results", nullptr, flags))
        {
            apply_pending_scroll(in);
            snap_scroll_to_line(in);

            const bool has_source = search.has_source();
            const auto source_bytes = has_source ? search.source_bytes() : std::span<const char>{};

            if (in.file_load.loading)
            {
                draw_loading_spinner(in.file_load.loading_path, in.file_load.loading_error);
            }
            else if (!has_source || source_bytes.empty())
            {
                draw_empty_state(in);
            }
            else
            {
                draw_results_body(in, search, source_bytes);
            }

            update_visible_range(in);
        }

        ImGui::End();
    }

} // namespace kestrel
