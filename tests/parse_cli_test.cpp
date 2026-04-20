#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "kestrel/util.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using kestrel::parse_cli;

namespace
{

    class TempFile
    {
    public:
        explicit TempFile(std::string_view content = "x")
        {
            path_ = std::filesystem::temp_directory_path() /
                    ("kestrel_cli_test_" + std::to_string(::getpid()) + "_" +
                     std::to_string(counter_++));
            std::ofstream ofs(path_, std::ios::binary);
            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        }
        ~TempFile()
        {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }

        std::string str() const { return path_.string(); }

    private:
        std::filesystem::path path_;
        static inline int counter_ = 0;
    };

    // Build argv that outlives the parse call.
    struct Argv
    {
        std::vector<std::string> storage;
        std::vector<char *> argv;
        Argv(std::initializer_list<std::string> args) : storage(args)
        {
            for (auto &s : storage)
                argv.push_back(s.data());
        }
        int argc() const { return static_cast<int>(argv.size()); }
        char **data() { return argv.data(); }
    };

} // namespace

TEST_CASE("no args returns default CliArgs")
{
    Argv a{"kestrel"};
    std::ostringstream err;
    auto r = parse_cli(a.argc(), a.data(), err);
    REQUIRE(r);
    CHECK(!r->file_path);
    CHECK(!r->show_help);
}

TEST_CASE("-h sets show_help")
{
    Argv a{"kestrel", "-h"};
    std::ostringstream err;
    auto r = parse_cli(a.argc(), a.data(), err);
    REQUIRE(r);
    CHECK(r->show_help);
}

TEST_CASE("--help sets show_help")
{
    Argv a{"kestrel", "--help"};
    std::ostringstream err;
    auto r = parse_cli(a.argc(), a.data(), err);
    REQUIRE(r);
    CHECK(r->show_help);
}

TEST_CASE("--file with valid path sets file_path")
{
    TempFile tf("content");
    Argv a{"kestrel", "--file", tf.str()};
    std::ostringstream err;
    auto r = parse_cli(a.argc(), a.data(), err);
    REQUIRE(r);
    REQUIRE(r->file_path);
    CHECK(*r->file_path == tf.str());
    CHECK(err.str().empty());
}

TEST_CASE("--file with missing value returns nullopt and writes error")
{
    Argv a{"kestrel", "--file"};
    std::ostringstream err;
    auto r = parse_cli(a.argc(), a.data(), err);
    CHECK(!r);
    CHECK(err.str().find("--file") != std::string::npos);
}

TEST_CASE("--file with nonexistent path returns nullopt")
{
    Argv a{"kestrel", "--file", "/nonexistent/kestrel/xyzzy"};
    std::ostringstream err;
    auto r = parse_cli(a.argc(), a.data(), err);
    CHECK(!r);
    CHECK(err.str().find("invalid") != std::string::npos);
}

TEST_CASE("unknown flag returns nullopt with error message")
{
    Argv a{"kestrel", "--bogus"};
    std::ostringstream err;
    auto r = parse_cli(a.argc(), a.data(), err);
    CHECK(!r);
    CHECK(err.str().find("unknown") != std::string::npos);
}
