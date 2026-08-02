#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "kestrel/search.hpp"

#include <cstdio>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

using kestrel::SearchController;

namespace
{

    class TempFile
    {
    public:
        explicit TempFile(std::string_view content)
        {
            path_ = std::filesystem::temp_directory_path() /
                    ("kestrel_sc_test_" + std::to_string(::getpid()) + "_" +
                     std::to_string(counter_++));
            std::ofstream ofs(path_, std::ios::binary);
            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        }
        ~TempFile()
        {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }
        TempFile(const TempFile &) = delete;
        TempFile &operator=(const TempFile &) = delete;

        std::string str() const { return path_.string(); }

    private:
        std::filesystem::path path_;
        static inline int counter_ = 0;
    };

    // Drive tick twice with debounce=0: first call stamps last_edit, second submits job.
    // Then wait for async completion.
    void rescan_now(SearchController &sc)
    {
        sc.set_debounce_ms(0);
        sc.tick(1.0);
        sc.tick(1.0);
        sc.wait_for_completion();
    }

} // namespace

TEST_CASE("load + pattern + tick produces matches")
{
    TempFile tf("foo bar\nfoo baz\nqux\n");
    SearchController sc;
    sc.load_source(tf.str());
    sc.set_pattern("foo", 0);
    rescan_now(sc);

    REQUIRE(sc.matches().size() == 2);
    CHECK(sc.matches()[0].start == 0);
    CHECK(sc.matches()[1].start == 8);
    CHECK(sc.matched_lines().size() == 2);
    CHECK(sc.compile_error().empty());
}

TEST_CASE("tick before debounce elapsed does not scan")
{
    TempFile tf("foo\n");
    SearchController sc;
    sc.load_source(tf.str());
    sc.set_pattern("foo", 0);
    sc.set_debounce_ms(1000);
    sc.tick(1.0); // stamp
    sc.tick(1.1); // 100 ms < 1000 ms debounce
    CHECK(sc.matches().empty());
    CHECK(sc.is_compiling()); // still dirty
}

TEST_CASE("wait_for_completion advances default debounce")
{
    TempFile tf("foo\n");
    SearchController sc;
    sc.load_source(tf.str());
    sc.set_pattern("foo", 0);
    sc.wait_for_completion();

    REQUIRE(sc.matches().size() == 1);
    CHECK(sc.compile_error().empty());
}

TEST_CASE("wait_for_completion completes without a source")
{
    SearchController sc;
    sc.set_pattern("foo", 0);

    sc.wait_for_completion();

    CHECK_FALSE(sc.is_compiling());
    CHECK(sc.matches().empty());
}

TEST_CASE("bad regex populates compile_error, leaves matches empty")
{
    TempFile tf("abc\n");
    SearchController sc;
    sc.load_source(tf.str());
    sc.set_pattern("[unclosed", 0);
    rescan_now(sc);
    CHECK(!sc.compile_error().empty());
    CHECK(sc.matches().empty());
}

TEST_CASE("prefilter rejects unbalanced paren")
{
    TempFile tf("abc\n");
    SearchController sc;
    sc.load_source(tf.str());
    sc.set_pattern("(unclosed", 0);
    rescan_now(sc);
    CHECK(sc.compile_error().find("unmatched") != std::string::npos);
    CHECK(sc.matches().empty());
}

TEST_CASE("prefilter rejects trailing backslash")
{
    TempFile tf("abc\n");
    SearchController sc;
    sc.load_source(tf.str());
    sc.set_pattern("foo\\", 0);
    rescan_now(sc);
    CHECK(sc.compile_error().find("trailing") != std::string::npos);
    CHECK(sc.matches().empty());
}

TEST_CASE("prefilter accepts escaped class chars")
{
    TempFile tf("a1b\n");
    SearchController sc;
    sc.load_source(tf.str());
    sc.set_pattern("[\\d]", 0);
    rescan_now(sc);
    CHECK(sc.compile_error().empty());
    REQUIRE(sc.matches().size() == 1);
}

TEST_CASE("compile cache reuses scanner across repeated patterns and evicts at cap")
{
    // 9 distinct patterns with cache size 8 forces one eviction. Re-running
    // pattern 0 after eviction should still succeed (recompile path).
    TempFile tf("abcdefghij\n");
    SearchController sc;
    sc.load_source(tf.str());
    const char* pats[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i"};
    for (const char* p : pats)
    {
        sc.set_pattern(p, 0);
        rescan_now(sc);
        REQUIRE(sc.compile_error().empty());
        CHECK(sc.matches().size() == 1);
    }
    // Pattern 0 ("a") was evicted; re-running must recompile cleanly.
    sc.set_pattern("a", 0);
    rescan_now(sc);
    CHECK(sc.compile_error().empty());
    CHECK(sc.matches().size() == 1);
}

TEST_CASE("clear_source drops source and matches")
{
    TempFile tf("foo\n");
    SearchController sc;
    sc.load_source(tf.str());
    sc.set_pattern("foo", 0);
    rescan_now(sc);
    REQUIRE(!sc.matches().empty());

    sc.clear_source();
    CHECK(!sc.has_source());
    CHECK(sc.matches().empty());
}

TEST_CASE("clear_source cancels the visible loading state")
{
    TempFile tf("foo\n");
    SearchController sc;

    sc.load_source_async(tf.str());
    REQUIRE(sc.is_loading());

    sc.clear_source();

    CHECK_FALSE(sc.is_loading());
    CHECK(sc.get_loading_error().empty());
    CHECK_FALSE(sc.has_source());
}

TEST_CASE("cancel_loading invalidates an asynchronous load")
{
    TempFile tf("foo\n");
    SearchController sc;

    sc.load_source_async(tf.str());
    REQUIRE(sc.is_loading());
    sc.cancel_loading();

    CHECK_FALSE(sc.is_loading());
    CHECK(sc.load_progress().phase == kestrel::SearchWorker::LoadPhase::Cancelled);
    // The worker may already be unwinding, but its stale result must not land.
    sc.tick(1.0);
    CHECK_FALSE(sc.has_source());
}

TEST_CASE("empty pattern clears matches on rescan")
{
    TempFile tf("foo\n");
    SearchController sc;
    sc.load_source(tf.str());
    sc.set_pattern("foo", 0);
    rescan_now(sc);
    REQUIRE(sc.matches().size() == 1);

    sc.set_pattern("", 0);
    rescan_now(sc);
    CHECK(sc.matches().empty());
    CHECK(sc.pattern_empty());
}

TEST_CASE("load_source resets matches but retains pattern")
{
    TempFile a("foo\n");
    TempFile b("bar\n");
    SearchController sc;
    sc.load_source(a.str());
    sc.set_pattern("foo", 0);
    rescan_now(sc);
    REQUIRE(sc.matches().size() == 1);

    sc.load_source(b.str());
    // load_source clears pattern_ and matches_; dirty_ set so next tick rescans
    rescan_now(sc);
    CHECK(sc.matches().empty());
    CHECK(sc.pattern_empty());
}

TEST_CASE("matches_before / matches_after are binary searchable on sorted matches")
{
    TempFile tf("xx foo yy foo zz foo\n");
    //          01234567890123456789
    SearchController sc;
    sc.load_source(tf.str());
    sc.set_pattern("foo", 0);
    rescan_now(sc);
    REQUIRE(sc.matches().size() == 3);
    // starts: 3, 10, 17
    CHECK(sc.matches_before(10) == 1); // only start=3 is < 10
    CHECK(sc.matches_after(10) == 1);  // only start=17 is > 10
    CHECK(sc.matches_before(0) == 0);
    CHECK(sc.matches_after(20) == 0);
    CHECK(sc.matches_before(100) == 3);
}

TEST_CASE("search controller exposes limited result sets")
{
    TempFile tf("foo foo foo foo\n");
    SearchController sc;
    sc.load_source(tf.str());
    sc.set_match_limit(2);
    sc.set_pattern("foo", 0);
    rescan_now(sc);

    REQUIRE(sc.matches().size() == 2);
    CHECK(sc.results_truncated());
    CHECK(sc.matches_before(100) == 2);
}

TEST_CASE("stale scan result is ignored after pattern changes")
{
    TempFile tf("foo\nbar\n");
    SearchController sc;
    sc.load_source(tf.str());
    sc.set_debounce_ms(0);

    sc.set_pattern("foo", 0);
    sc.tick(1.0);
    sc.tick(1.0);

    sc.set_pattern("bar", 0);
    sc.wait_for_completion();

    REQUIRE(sc.matches().size() == 1);
    CHECK(sc.matches()[0].start == 4);
    CHECK(sc.compile_error().empty());
}

TEST_CASE("follow mode refreshes on inotify append and defers it while paused")
{
    TempFile tf("one\n");
    SearchController sc;
    sc.load_source(tf.str());
    sc.set_tail_mode(true);

    {
        std::ofstream out(tf.str(), std::ios::app | std::ios::binary);
        out << "two\n";
    }
    for (int i = 0; i < 100 && sc.source_bytes().size() != 8; ++i)
    {
        sc.tick(1.0 + i * 0.001);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(sc.source_bytes().size() == 8);
    CHECK(sc.line_index().line_count() == 2);

    sc.set_tail_paused(true);
    {
        std::ofstream out(tf.str(), std::ios::app | std::ios::binary);
        out << "three\n";
    }
    for (int i = 0; i < 10; ++i)
    {
        sc.tick(2.0 + i * 0.001);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(sc.source_bytes().size() == 8);

    sc.set_tail_paused(false);
    for (int i = 0; i < 100 && sc.source_bytes().size() != 14; ++i)
    {
        sc.tick(3.0 + i * 0.001);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(sc.source_bytes().size() == 14);
    CHECK(sc.line_index().line_count() == 3);
}

TEST_CASE("follow mode rebuilds its line index after a larger file replacement")
{
    TempFile tf("a\nb\n");
    SearchController sc;
    sc.load_source(tf.str());
    sc.set_tail_mode(true);

    {
        std::ofstream out(tf.str(), std::ios::trunc | std::ios::binary);
        out << "alpha\nbeta\n";
    }
    for (int i = 0; i < 100 && sc.source_bytes().size() != 11; ++i)
    {
        sc.tick(1.0 + i * 0.001);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE(sc.source_bytes().size() == 11);
    CHECK(sc.line_index().line_count() == 2);
    CHECK(sc.line_index().line_start(0) == 0);
    CHECK(sc.line_index().line_start(1) == 6);
}
