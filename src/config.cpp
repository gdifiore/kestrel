#include "kestrel/config.hpp"

#include "kestrel/ui.hpp"

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
        std::ifstream config(config_path());

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
                    (void)parse_bool(val, in.case_sensitive);
                else if (key == "show_line_nums")
                    (void)parse_bool(val, in.show_line_nums);
                else if (key == "snap_scroll")
                    (void)parse_bool(val, in.snap_scroll);
                else if (key == "color_match")
                    (void)parse_color(val, in.color_match);
                else if (key == "color_scope")
                    (void)parse_color(val, in.color_scope);
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

        if (!config) return false;

        write_bool(config, "case_sensitive", in.case_sensitive);
        write_bool(config, "show_line_nums", in.show_line_nums);
        write_bool(config, "snap_scroll", in.snap_scroll);
        write_color(config, "color_match", in.color_match);
        write_color(config, "color_scope", in.color_scope);

        config.close();

        if (std::rename(temp.c_str(), destination.c_str()) == 0)
            return true;
        std::remove(temp.c_str()); // cleanup on rename failure
        return false;
    }

} // namespace kestrel
