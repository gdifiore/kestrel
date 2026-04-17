#include "kestrel/util.hpp"

#include <cstring>
#include <filesystem>
#include <ostream>
#include <system_error>

namespace kestrel {

  bool is_valid_file_path(std::string_view path) {
      if (path.empty()) return false;
      if (path.find('\0') != std::string_view::npos) return false;

      std::error_code ec;
      std::filesystem::path p{path};
      auto status = std::filesystem::status(p, ec);
      if (ec) return false;
      return std::filesystem::is_regular_file(status);
  }

  void print_usage(std::ostream& os, const char* prog) {
      os << "usage: " << (prog ? prog : "kestrel")
         << " [--file <path>]\n"
         << "  --file <path>   load file on startup\n"
         << "  -h, --help      show this help\n";
  }

  std::optional<CliArgs> parse_cli(int argc, char** argv, std::ostream& err) {
      CliArgs args;
      const char* prog = argc > 0 ? argv[0] : "kestrel";

      if (argc <= 1) return args;

      const char* flag = argv[1];
      if (std::strcmp(flag, "-h") == 0 || std::strcmp(flag, "--help") == 0) {
          args.show_help = true;
          return args;
      }
      if (std::strcmp(flag, "--file") == 0) {
          if (argc < 3) {
              err << "error: --file requires a path\n";
              print_usage(err, prog);
              return std::nullopt;
          }
          const char* path = argv[2];
          if (!is_valid_file_path(path)) {
              err << "error: invalid file path: " << path << "\n";
              return std::nullopt;
          }
          args.file_path = path;
          return args;
      }
      err << "error: unknown argument: " << flag << "\n";
      print_usage(err, prog);
      return std::nullopt;
  }

} // namespace kestrel
