#include "kestrel/config.hpp"

#include "kestrel/ui.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <fstream>

namespace kestrel
{

    namespace
    {
        std::string_view trim(std::string_view s)
        {
            const auto ws = " \t\r";
            auto a = s.find_first_not_of(ws);
            if (a == std::string_view::npos)
                return {};
            auto b = s.find_last_not_of(ws);
            return s.substr(a, b - a + 1);
        }

        bool parse_bool(std::string_view val, bool &out)
        {
            if (val == "1" || val == "true" || val == "yes")
            {
                out = true;
                return true;
            }
            if (val == "0" || val == "false" || val == "no")
            {
                out = false;
                return true;
            }
            return false;
        }

        bool parse_color(std::string_view val, ImVec4 &out)
        {
            float c[4];
            int i = 0;
            size_t pos = 0;
            while (i < 4 && pos < val.size())
            {
                size_t comma = val.find(',', pos);
                std::string tok(val.substr(pos, comma - pos));
                try
                {
                    c[i++] = std::stof(tok);
                }
                catch (...)
                {
                    return false;
                }
                if (comma == std::string_view::npos)
                    break;
                pos = comma + 1;
            }
            if (i != 4)
                return false;
            out = {c[0], c[1], c[2], c[3]};
            return true;
        }

        void write_bool(std::ofstream &config_file, std::string_view key, bool val)
        {
            config_file << key << "=" << val << "\n";
        }

        void write_color(std::ofstream &config_file, std::string_view key, ImVec4 val)
        {
            config_file << key << "=" << std::format("{:.3f},{:.3f},{:.3f},{:.3f}", val.x, val.y, val.z, val.w) << "\n";
        }
    } // namespace

    std::filesystem::path config_path()
    {
        namespace fs = std::filesystem;
        if (const char *x = std::getenv("XDG_CONFIG_HOME"); x && *x)
            return fs::path(x) / "kestrel" / "config.ini";
        if (const char *h = std::getenv("HOME"); h && *h)
            return fs::path(h) / ".config" / "kestrel" / "config.ini";
        return fs::path("kestrel.ini"); // last-resort cwd
    }

    void load_config(UiInputs &in)
    {
        auto path = config_path();
        std::ifstream config(path);
        if (!config)
        {
            spdlog::debug("no config at {}", path.string());
            return;
        }
        spdlog::debug("load config {}", path.string());

        for (std::string line; getline(config, line);)
        {
            if (line.starts_with("#"))
                continue;

            size_t pos = line.find('=');

            if (pos != std::string::npos)
            {
                std::string_view key = trim(std::string_view(line).substr(0, pos));
                std::string_view val = trim(std::string_view(line).substr(pos + 1));

                if (key == "case_sensitive")
                    (void)parse_bool(val, in.search.case_sensitive);
                else if (key == "dotall")
                    (void)parse_bool(val, in.search.dotall);
                else if (key == "multiline")
                    (void)parse_bool(val, in.search.multiline);
                else if (key == "show_line_nums")
                    (void)parse_bool(val, in.view.show_line_nums);
                else if (key == "snap_scroll")
                    (void)parse_bool(val, in.view.snap_scroll);
                else if (key == "color_match")
                    (void)parse_color(val, in.view.color_match);
                else if (key == "color_scope")
                    (void)parse_color(val, in.view.color_scope);
                else if (key.starts_with("recent_file_"))
                {
                    // Parse recent_file_0, recent_file_1, etc.
                    in.file.recent_files.emplace_back(val);
                }
            }
        }
    }

    bool save_config(const UiInputs &in)
    {
        auto destination = config_path();
        auto temp = std::string(destination) + ".tmp";

        std::error_code ec;
        std::filesystem::create_directories(destination.parent_path(), ec);

        std::ofstream config(temp);

        if (!config)
        {
            spdlog::warn("save_config: open failed {}", temp);
            return false;
        }

        write_bool(config, "case_sensitive", in.search.case_sensitive);
        write_bool(config, "dotall", in.search.dotall);
        write_bool(config, "multiline", in.search.multiline);
        write_bool(config, "show_line_nums", in.view.show_line_nums);
        write_bool(config, "snap_scroll", in.view.snap_scroll);
        write_color(config, "color_match", in.view.color_match);
        write_color(config, "color_scope", in.view.color_scope);

        // Save recent files (limit to 10)
        for (size_t i = 0; i < std::min(in.file.recent_files.size(), size_t(10)); ++i)
        {
            config << "recent_file_" << i << " = " << in.file.recent_files[i] << "\n";
        }

        config.close();

        if (std::rename(temp.c_str(), destination.c_str()) == 0)
            return true;
        spdlog::warn("save_config: rename failed {} -> {}", temp, destination.string());
        std::remove(temp.c_str()); // cleanup on rename failure
        return false;
    }

    void add_recent_file(UiInputs &ui, const std::string &path)
    {
        auto &recent = ui.file.recent_files;
        recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
        recent.insert(recent.begin(), path);
        if (recent.size() > 10)
        {
            recent.resize(10);
        }
    }

    void cleanup_recent_files(UiInputs &ui)
    {
        auto &recent = ui.file.recent_files;
        recent.erase(
            std::remove_if(recent.begin(), recent.end(),
                           [](const std::string &path)
                           {
                               return !std::filesystem::exists(path);
                           }),
            recent.end());
    }

} // namespace kestrel
