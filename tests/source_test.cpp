#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "kestrel/source.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

using kestrel::Source;
using kestrel::SourceError;

namespace
{

    class TempFile
    {
    public:
        explicit TempFile(std::string_view content)
        {
            path_ = std::filesystem::temp_directory_path() /
                    ("kestrel_src_test_" + std::to_string(::getpid()) + "_" +
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

} // namespace

TEST_CASE("from_path loads file contents")
{
    TempFile tf("hello world");
    Source s = Source::from_path(tf.str());
    auto b = s.bytes();
    REQUIRE(b.size() == 11);
    CHECK(std::string_view(b.data(), b.size()) == "hello world");
}

TEST_CASE("from_path on empty file returns empty Source")
{
    TempFile tf("");
    Source s = Source::from_path(tf.str());
    CHECK(s.bytes().empty());
    CHECK(s.bytes().data() == nullptr);
}

TEST_CASE("from_path on nonexistent path throws SourceError")
{
    CHECK_THROWS_AS(Source::from_path("/nonexistent/kestrel/xyzzy"), SourceError);
}

TEST_CASE("from_path error message includes syscall name")
{
    try
    {
        Source::from_path("/nonexistent/kestrel/xyzzy");
        FAIL("expected throw");
    }
    catch (const SourceError &e)
    {
        CHECK(std::string(e.what()).find("open") != std::string::npos);
    }
}

TEST_CASE("from_path preserves binary content with embedded NULs")
{
    std::string payload("a\0b\0c", 5);
    TempFile tf(payload);
    Source s = Source::from_path(tf.str());
    auto b = s.bytes();
    REQUIRE(b.size() == 5);
    CHECK(std::string(b.data(), b.size()) == payload);
}

TEST_CASE("move construction transfers mapping")
{
    TempFile tf("abc");
    Source a = Source::from_path(tf.str());
    const char *orig = a.bytes().data();
    std::size_t orig_size = a.bytes().size();

    Source b(std::move(a));
    CHECK(a.bytes().data() == nullptr);
    CHECK(a.bytes().size() == 0);
    CHECK(b.bytes().data() == orig);
    CHECK(b.bytes().size() == orig_size);
    CHECK(std::string_view(b.bytes().data(), b.bytes().size()) == "abc");
}

TEST_CASE("move assignment releases old mapping and takes new")
{
    TempFile tf_a("aaa");
    TempFile tf_b("bbbb");
    Source a = Source::from_path(tf_a.str());
    Source b = Source::from_path(tf_b.str());

    a = std::move(b);
    CHECK(a.bytes().size() == 4);
    CHECK(std::string_view(a.bytes().data(), a.bytes().size()) == "bbbb");
    CHECK(b.bytes().data() == nullptr);
    CHECK(b.bytes().size() == 0);
}

TEST_CASE("larger file size matches")
{
    std::string payload(256 * 1024, 'x');
    TempFile tf(payload);
    Source s = Source::from_path(tf.str());
    REQUIRE(s.bytes().size() == payload.size());
    CHECK(s.bytes().front() == 'x');
    CHECK(s.bytes().back() == 'x');
}
