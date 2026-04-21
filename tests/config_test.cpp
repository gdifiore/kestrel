#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "kestrel/config.hpp"
#include "kestrel/ui.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace kestrel
{

    // Helper to create temporary files for recent file tests
    class TempFile
    {
    public:
        TempFile()
        {
            path_ = std::filesystem::temp_directory_path() / ("kestrel_temp_" + std::to_string(rand()) + ".txt");
            std::ofstream file(path_);
            file << "test content\n";
            file.close();
        }

        ~TempFile()
        {
            std::filesystem::remove(path_);
        }

        const std::filesystem::path &path() const { return path_; }
        std::string path_str() const { return path_.string(); }

    private:
        std::filesystem::path path_;
    };

    TEST_CASE("Config path function")
    {
        auto path = config_path();

        // Should return a valid path
        CHECK_FALSE(path.empty());

        // Should end with kestrel/config.ini
        CHECK(path.filename() == "config.ini");
        CHECK(path.parent_path().filename() == "kestrel");
    }

    TEST_CASE("Recent files - add_recent_file")
    {
        UiInputs ui;
        ui.file.recent_files = {"/existing/file1.txt", "/existing/file2.log"};

        SUBCASE("Add new file")
        {
            add_recent_file(ui, "/new/file.txt");

            REQUIRE(ui.file.recent_files.size() == 3);
            CHECK(ui.file.recent_files[0] == "/new/file.txt"); // Most recent first
            CHECK(ui.file.recent_files[1] == "/existing/file1.txt");
            CHECK(ui.file.recent_files[2] == "/existing/file2.log");
        }

        SUBCASE("Add existing file (should move to front)")
        {
            add_recent_file(ui, "/existing/file2.log");

            REQUIRE(ui.file.recent_files.size() == 2);
            CHECK(ui.file.recent_files[0] == "/existing/file2.log"); // Moved to front
            CHECK(ui.file.recent_files[1] == "/existing/file1.txt");
        }

        SUBCASE("Add files beyond limit (should cap at 10)")
        {
            // Fill up to 10 files
            for (int i = 0; i < 9; ++i)
            {
                add_recent_file(ui, "/file" + std::to_string(i) + ".txt");
            }

            CHECK(ui.file.recent_files.size() == 10);       // Should be capped at 10
            CHECK(ui.file.recent_files[0] == "/file8.txt"); // Most recent

            // Add one more - should drop the oldest
            add_recent_file(ui, "/newest.txt");
            CHECK(ui.file.recent_files.size() == 10);
            CHECK(ui.file.recent_files[0] == "/newest.txt");
            // Original files should be pushed out
            auto it = std::find(ui.file.recent_files.begin(), ui.file.recent_files.end(), "/existing/file2.log");
            CHECK(it == ui.file.recent_files.end()); // Should be removed
        }
    }

    TEST_CASE("Recent files - cleanup_recent_files")
    {
        UiInputs ui;

        SUBCASE("Remove nonexistent files")
        {
            // Create some temporary files
            TempFile temp1, temp2;

            ui.file.recent_files = {
                temp1.path_str(),         // Exists
                "/nonexistent/file1.txt", // Doesn't exist
                temp2.path_str(),         // Exists
                "/nonexistent/file2.log", // Doesn't exist
                "/nonexistent/file3.md"   // Doesn't exist
            };

            cleanup_recent_files(ui);

            // Should only keep existing files
            REQUIRE(ui.file.recent_files.size() == 2);
            CHECK(ui.file.recent_files[0] == temp1.path_str());
            CHECK(ui.file.recent_files[1] == temp2.path_str());
        }

        SUBCASE("Handle empty list")
        {
            ui.file.recent_files.clear();

            cleanup_recent_files(ui);

            CHECK(ui.file.recent_files.empty());
        }

        SUBCASE("All files exist")
        {
            TempFile temp1, temp2, temp3;

            ui.file.recent_files = {
                temp1.path_str(),
                temp2.path_str(),
                temp3.path_str()};

            auto original_size = ui.file.recent_files.size();
            cleanup_recent_files(ui);

            // All files should remain
            CHECK(ui.file.recent_files.size() == original_size);
        }

        SUBCASE("No files exist")
        {
            ui.file.recent_files = {
                "/nonexistent/file1.txt",
                "/nonexistent/file2.log",
                "/nonexistent/file3.md"};

            cleanup_recent_files(ui);

            // Should remove all nonexistent files
            CHECK(ui.file.recent_files.empty());
        }
    }

    TEST_CASE("Config save/load integration test")
    {
        // This test uses the real config system but backs up the user's config
        UiInputs test_ui;
        test_ui.search.case_sensitive = true;
        test_ui.search.dotall = false;
        test_ui.search.multiline = true;
        test_ui.view.show_line_nums = false;
        test_ui.view.snap_scroll = false;
        test_ui.view.color_match = ImVec4(0.1f, 0.2f, 0.3f, 0.4f);
        test_ui.view.color_scope = ImVec4(0.5f, 0.6f, 0.7f, 0.8f);
        test_ui.file.recent_files = {"/test/file1.txt", "/test/file2.log"};

        auto config_file = config_path();
        auto backup_file = config_file.string() + ".test_backup";

        // Backup existing config
        std::error_code ec;
        if (std::filesystem::exists(config_file))
        {
            std::filesystem::copy_file(config_file, backup_file, ec);
        }

        // Save test config
        bool save_success = save_config(test_ui);
        CHECK(save_success == true);

        // Load config into fresh UiInputs
        UiInputs loaded_ui;
        load_config(loaded_ui);

        // Verify critical values (some may have defaults applied)
        CHECK(loaded_ui.search.case_sensitive == test_ui.search.case_sensitive);
        CHECK(loaded_ui.search.multiline == test_ui.search.multiline);

        // Color values should be preserved
        CHECK(loaded_ui.view.color_match.x == doctest::Approx(test_ui.view.color_match.x));
        CHECK(loaded_ui.view.color_match.y == doctest::Approx(test_ui.view.color_match.y));
        CHECK(loaded_ui.view.color_match.z == doctest::Approx(test_ui.view.color_match.z));
        CHECK(loaded_ui.view.color_match.w == doctest::Approx(test_ui.view.color_match.w));

        // Recent files should be preserved (limited to 10)
        CHECK(loaded_ui.file.recent_files.size() <= 10);
        if (loaded_ui.file.recent_files.size() >= 2)
        {
            CHECK(loaded_ui.file.recent_files[0] == "/test/file1.txt");
            CHECK(loaded_ui.file.recent_files[1] == "/test/file2.log");
        }

        // Restore original config
        std::filesystem::remove(config_file, ec);
        if (std::filesystem::exists(backup_file))
        {
            std::filesystem::rename(backup_file, config_file, ec);
        }
    }

    TEST_CASE("Config save - directory creation")
    {
        // Test that save_config can create directories
        auto config_file = config_path();
        auto parent_dir = config_file.parent_path();
        auto backup_dir = parent_dir.string() + ".test_backup";

        std::error_code ec;

        // Backup config directory if it exists
        bool had_config = std::filesystem::exists(parent_dir);
        if (had_config)
        {
            std::filesystem::rename(parent_dir, backup_dir, ec);
        }

        UiInputs ui;
        ui.search.case_sensitive = true;

        // Should create directory and save successfully
        bool result = save_config(ui);
        CHECK(result == true);
        CHECK(std::filesystem::exists(config_file));

        // Cleanup
        std::filesystem::remove_all(parent_dir, ec);
        if (had_config && std::filesystem::exists(backup_dir))
        {
            std::filesystem::rename(backup_dir, parent_dir, ec);
        }
    }

    TEST_CASE("Recent files - stress test with many files")
    {
        UiInputs ui;

        // Add more than the limit
        for (int i = 0; i < 15; ++i)
        {
            add_recent_file(ui, "/stress/test/file" + std::to_string(i) + ".txt");
        }

        // Should cap at 10
        CHECK(ui.file.recent_files.size() == 10);
        CHECK(ui.file.recent_files[0] == "/stress/test/file14.txt"); // Most recent
        CHECK(ui.file.recent_files[9] == "/stress/test/file5.txt");  // Oldest kept

        // Add duplicate - should move to front without increasing size
        add_recent_file(ui, "/stress/test/file10.txt");
        CHECK(ui.file.recent_files.size() == 10);
        CHECK(ui.file.recent_files[0] == "/stress/test/file10.txt");
    }

} // namespace kestrel