#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "kestrel/search.hpp"
#include "kestrel/ui.hpp"

#include <filesystem>
#include <fstream>
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
