#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "kestrel/scanner.hpp"

#include <hs.h>
#include <string>
#include <utility>

using kestrel::Scanner;
using kestrel::ScannerError;

TEST_CASE("literal match — overlapping repeats") {
    Scanner s("foo");
    auto hits = s.scan("foofoo");
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].start == 0);
    CHECK(hits[0].end   == 3);
    CHECK(hits[1].start == 3);
    CHECK(hits[1].end   == 6);
}

TEST_CASE("alternation") {
    Scanner s("cat|dog");
    auto hits = s.scan("a cat and a dog");
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].start == 2);
    CHECK(hits[0].end   == 5);
    CHECK(hits[1].start == 12);
    CHECK(hits[1].end   == 15);
}

TEST_CASE("anchored without multiline — only first line") {
    Scanner s("^err");
    auto hits = s.scan("err\nok\nerr");
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].start == 0);
    CHECK(hits[0].end   == 3);
}

TEST_CASE("anchored with multiline — every line start") {
    Scanner s("^err", HS_FLAG_MULTILINE);
    auto hits = s.scan("err\nok\nerr");
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].start == 0);
    CHECK(hits[1].start == 7);
}

TEST_CASE("no match returns empty, no throw") {
    Scanner s("zzz");
    auto hits = s.scan("abcdef");
    CHECK(hits.empty());
}

TEST_CASE("invalid pattern throws ScannerError") {
    CHECK_THROWS_AS(Scanner("[unclosed"), ScannerError);
}

TEST_CASE("invalid pattern error message non-empty") {
    try {
        Scanner s("[unclosed");
        FAIL("expected throw");
    } catch (const ScannerError& e) {
        CHECK(std::string(e.what()).find("hs_compile") != std::string::npos);
    }
}

TEST_CASE("move construction preserves scan behavior") {
    Scanner a("foo");
    Scanner b(std::move(a));
    auto hits = b.scan("foofoo");
    CHECK(hits.size() == 2);
}

TEST_CASE("move assignment preserves scan behavior") {
    Scanner a("foo");
    Scanner b("bar");
    b = std::move(a);
    auto hits = b.scan("foofoo");
    CHECK(hits.size() == 2);
}
