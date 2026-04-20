#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "kestrel/line_index.hpp"

#include <string>
#include <string_view>

using kestrel::LineIndex;

static std::span<const char> view(std::string_view s)
{
    return {s.data(), s.size()};
}

TEST_CASE("empty buffer has one line starting at 0")
{
    LineIndex li(view(""));
    CHECK(li.line_count() == 1);
    CHECK(li.line_start(0) == 0);
}

TEST_CASE("single line without newline")
{
    LineIndex li(view("hello"));
    CHECK(li.line_count() == 1);
    CHECK(li.line_of(0) == 0);
    CHECK(li.line_of(4) == 0);
}

TEST_CASE("three lines, no trailing newline")
{
    //           0123 456 789
    //           "aaa\nbb\nccc"
    LineIndex li(view("aaa\nbb\nccc"));
    REQUIRE(li.line_count() == 3);
    CHECK(li.line_start(0) == 0);
    CHECK(li.line_start(1) == 4);
    CHECK(li.line_start(2) == 7);

    CHECK(li.line_of(0) == 0);
    CHECK(li.line_of(2) == 0);
    CHECK(li.line_of(3) == 0); // the '\n' itself belongs to line 0
    CHECK(li.line_of(4) == 1); // first byte of line 1
    CHECK(li.line_of(5) == 1);
    CHECK(li.line_of(6) == 1); // second '\n'
    CHECK(li.line_of(7) == 2);
    CHECK(li.line_of(9) == 2);
}

TEST_CASE("trailing newline does not create phantom line")
{
    LineIndex li(view("a\nb\n"));
    // lines: "a\n", "b\n" — two lines, not three
    REQUIRE(li.line_count() == 2);
    CHECK(li.line_start(0) == 0);
    CHECK(li.line_start(1) == 2);
    CHECK(li.line_of(0) == 0);
    CHECK(li.line_of(1) == 0);
    CHECK(li.line_of(2) == 1);
    CHECK(li.line_of(3) == 1);
}

TEST_CASE("consecutive newlines produce empty lines")
{
    //           0 1 2 3
    //           "\n\nx\n"
    LineIndex li(view("\n\nx"));
    REQUIRE(li.line_count() == 3);
    CHECK(li.line_start(0) == 0);
    CHECK(li.line_start(1) == 1);
    CHECK(li.line_start(2) == 2);
    CHECK(li.line_of(0) == 0);
    CHECK(li.line_of(1) == 1);
    CHECK(li.line_of(2) == 2);
}

TEST_CASE("offset exactly at line start maps to that line")
{
    LineIndex li(view("a\nb\nc"));
    CHECK(li.line_of(2) == 1); // start of line 1
    CHECK(li.line_of(4) == 2); // start of line 2
}

TEST_CASE("CRLF line endings — \\r stays on previous line")
{
    //           0 1 2 3 4 5
    //           a \r\n b \r\n
    LineIndex li(view("a\r\nb\r\n"));
    REQUIRE(li.line_count() == 2);
    CHECK(li.line_start(0) == 0);
    CHECK(li.line_start(1) == 3);
    CHECK(li.line_of(0) == 0); // 'a'
    CHECK(li.line_of(1) == 0); // '\r' part of line 0
    CHECK(li.line_of(2) == 0); // '\n' part of line 0
    CHECK(li.line_of(3) == 1); // 'b'
    CHECK(li.line_of(5) == 1); // '\n' of line 1
}

TEST_CASE("single huge line, no newlines")
{
    std::string buf(1'000'000, 'x');
    LineIndex li(view(buf));
    CHECK(li.line_count() == 1);
    CHECK(li.line_start(0) == 0);
    CHECK(li.line_of(0) == 0);
    CHECK(li.line_of(buf.size() - 1) == 0);
}

TEST_CASE("many lines")
{
    std::string buf;
    for (int i = 0; i < 1000; ++i)
        buf += "x\n";
    buf += "end";
    LineIndex li(view(buf));
    CHECK(li.line_count() == 1001);
    CHECK(li.line_of(0) == 0);
    CHECK(li.line_of(1998) == 999);  // last 'x' of line 999
    CHECK(li.line_of(2000) == 1000); // 'e' of "end"
    CHECK(li.line_of(2002) == 1000); // 'd'
}
