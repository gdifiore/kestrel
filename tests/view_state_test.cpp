#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "kestrel/search.hpp"
#include "kestrel/ui.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

namespace
{
    struct TempFile
    {
        std::filesystem::path path;
        explicit TempFile(const char *text)
        {
            static unsigned counter = 0;
            path = std::filesystem::temp_directory_path() /
                   ("kestrel_view_test_" + std::to_string(getpid()) + "_" +
                    std::to_string(counter++));
            std::ofstream out(path);
            out << text;
        }
        ~TempFile() { std::filesystem::remove(path); }
    };

    bool wait_for_lines(kestrel::SearchController &search, std::size_t count)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (search.line_index().line_count() != count &&
               std::chrono::steady_clock::now() < deadline)
        {
            const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
            search.tick(std::chrono::duration<double>(elapsed).count());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return search.line_index().line_count() == count;
    }
}

TEST_CASE("source replacement refreshes custom view before input and clamps positions")
{
    TempFile old_file("2026-11-05T12:00:00Z first\n"
                      "2026-11-05T12:00:01Z second\n"
                      "2026-11-05T12:00:02Z third\n");
    TempFile new_file("2026-11-05T12:00:00Z only\n");
    kestrel::SearchController search;
    kestrel::UiInputs ui;
    search.load_source(old_file.path.string());
    auto &time = ui.layout.filters.time;
    time.active = true;
    time.start = search.timestamp_index().min_ts();
    time.end = search.timestamp_index().max_ts();
    kestrel::prepare_view_state(ui, search);
    REQUIRE(kestrel::make_view_index(ui, search).row_count() == 3);
    ui.cursor.line = 2;
    ui.selection.anchor_line = 2;

    search.load_source(new_file.path.string());
    kestrel::prepare_view_state(ui, search);
    const auto view = kestrel::make_view_index(ui, search);
    REQUIRE(view.row_count() == 1);
    CHECK(view.row_to_source(0) == 0);
    CHECK(ui.cursor.line == 0);
    CHECK(ui.cursor.offset == 0);
    CHECK_FALSE(ui.selection.anchor_line.has_value());
}

TEST_CASE("empty filtered view still clamps source positions after shrink")
{
    TempFile old_file("a\nb\nc\n");
    TempFile new_file("x\n");
    kestrel::SearchController search;
    kestrel::UiInputs ui;
    search.load_source(old_file.path.string());
    ui.cursor.line = 2;
    ui.cursor.offset = 4;
    ui.selection.anchor_line = 2;
    ui.view.display_only_filtered_lines = true;

    search.load_source(new_file.path.string());
    search.set_pattern("absent", 0);
    search.wait_for_completion();
    kestrel::prepare_view_state(ui, search);
    CHECK(kestrel::make_view_index(ui, search).row_count() == 0);
    CHECK(ui.cursor.line == 0);
    CHECK(ui.cursor.offset == 0);
    CHECK_FALSE(ui.selection.anchor_line.has_value());

    search.clear_source();
    kestrel::prepare_view_state(ui, search);
    CHECK(kestrel::make_view_index(ui, search).row_count() == 0);
}

TEST_CASE("time filter bounds follow source changes without rendering the Options popup")
{
    TempFile old_file("2026-11-05T12:00:00Z first\n"
                      "2026-11-05T12:00:01Z second\n"
                      "2026-11-05T12:00:02Z third\n");
    TempFile new_file("2026-11-06T13:00:00Z first\n"
                      "2026-11-06T13:00:01Z second\n");
    kestrel::SearchController search;
    kestrel::UiInputs ui;
    search.load_source(old_file.path.string());
    auto &time = ui.layout.filters.time;
    kestrel::prepare_view_state(ui, search);
    time.start = time.end = search.timestamp_index().at(1);

    SUBCASE("active filter") { time.active = true; }
    SUBCASE("inactive filter") { time.active = false; }

    search.load_source(new_file.path.string());
    kestrel::prepare_view_state(ui, search);
    CHECK(time.start == search.timestamp_index().min_ts());
    CHECK(time.end == search.timestamp_index().max_ts());
    time.active = true;
    kestrel::prepare_view_state(ui, search);
    CHECK(kestrel::make_view_index(ui, search).row_count() == 2);
}

TEST_CASE("ordinary reload resets narrowed time bounds even when timestamp ranges match")
{
    TempFile file("2026-11-05T12:00:00Z first\n"
                  "2026-11-05T12:00:01Z second\n"
                  "2026-11-05T12:00:02Z third\n");
    kestrel::SearchController search;
    kestrel::UiInputs ui;
    search.load_source(file.path.string());
    auto &time = ui.layout.filters.time;
    time.active = true;
    kestrel::prepare_view_state(ui, search);
    time.start = time.end = search.timestamp_index().at(1);

    search.load_source(file.path.string());
    kestrel::prepare_view_state(ui, search);
    CHECK(time.start == search.timestamp_index().min_ts());
    CHECK(time.end == search.timestamp_index().max_ts());
    CHECK(kestrel::make_view_index(ui, search).row_count() == 3);
}

TEST_CASE("query completion preserves a narrowed time selection")
{
    TempFile file("2026-11-05T12:00:00Z first\n"
                  "2026-11-05T12:00:01Z second\n");
    kestrel::SearchController search;
    kestrel::UiInputs ui;
    search.load_source(file.path.string());
    auto &time = ui.layout.filters.time;
    time.active = true;
    kestrel::prepare_view_state(ui, search);
    const auto selected = search.timestamp_index().at(1);
    time.start = time.end = selected;

    search.set_pattern("second", 0);
    search.wait_for_completion();
    kestrel::prepare_view_state(ui, search);
    CHECK(time.start == selected);
    CHECK(time.end == selected);
    CHECK(kestrel::make_view_index(ui, search).row_count() == 1);
}

#if defined(__linux__) || defined(__APPLE__)
TEST_CASE("follow updates extend the full time range and preserve narrowed selections")
{
    TempFile file("2026-11-05T12:00:00Z first\n"
                  "2026-11-05T12:00:01Z second\n"
                  "2026-11-05T12:00:02Z third\n");
    kestrel::SearchController search;
    kestrel::UiInputs ui;
    search.load_source(file.path.string());
    search.set_tail_mode(true);
    auto &time = ui.layout.filters.time;
    time.active = true;
    kestrel::prepare_view_state(ui, search);
    bool narrowed = false;
    SUBCASE("full range") {}
    SUBCASE("narrowed selection")
    {
        narrowed = true;
        time.start = time.end = search.timestamp_index().at(1);
    }
    const auto old_start = time.start;
    const auto old_end = time.end;
    const auto source_revision = search.source_revision();
    {
        std::ofstream out(file.path, std::ios::app);
        out << "2026-11-05T12:00:03Z fourth\n";
    }
    REQUIRE(wait_for_lines(search, 4));
    // Synchronization must remain correct after the one-tick follow notification
    // has expired, as can happen when the resize callback renders another frame.
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    search.tick(std::chrono::duration<double>(elapsed).count());
    kestrel::prepare_view_state(ui, search);
    CHECK(search.source_revision() == source_revision);
    CHECK(time.start == old_start);
    CHECK(time.end == (narrowed ? old_end : search.timestamp_index().max_ts()));
    CHECK(kestrel::make_view_index(ui, search).row_count() == (narrowed ? 1 : 4));
}

TEST_CASE("follow rewrite preserves a narrowed interval outside the new timestamp range")
{
    TempFile file("2026-11-05T12:00:00Z first\n"
                  "2026-11-05T12:00:01Z second\n"
                  "2026-11-05T12:00:02Z third\n");
    kestrel::SearchController search;
    kestrel::UiInputs ui;
    search.load_source(file.path.string());
    search.set_tail_mode(true);
    auto &time = ui.layout.filters.time;
    time.active = true;
    kestrel::prepare_view_state(ui, search);
    const auto selected = search.timestamp_index().at(1);
    time.start = time.end = selected;
    {
        std::ofstream out(file.path, std::ios::trunc);
        out << "2026-11-06T13:00:00Z first\n"
               "2026-11-06T13:00:01Z second\n";
    }
    REQUIRE(wait_for_lines(search, 2));
    kestrel::prepare_view_state(ui, search);
    CHECK(time.source_min > selected);
    CHECK(time.start == selected);
    CHECK(time.end == selected);
    CHECK(kestrel::make_view_index(ui, search).row_count() == 0);
}
#endif
