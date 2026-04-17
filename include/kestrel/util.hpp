#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace kestrel {

  bool is_valid_file_path(std::string_view path);

  struct CliArgs {
      std::optional<std::string> file_path;
      bool show_help = false;
  };

  // Returns parsed args on success. Returns nullopt on error or unknown flag
  // (message written to err). If --help/-h, show_help is set.
  std::optional<CliArgs> parse_cli(int argc, char** argv, std::ostream& err);

  void print_usage(std::ostream& os, const char* prog);

} // namespace kestrel
