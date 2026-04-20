#include "kestrel/app.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

int main(int argc, char **argv)
{
    auto logger = spdlog::stderr_color_mt("kestrel");
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    if (const char *lvl = std::getenv("KESTREL_LOG"))
        spdlog::set_level(spdlog::level::from_str(lvl));
    else
        spdlog::set_level(spdlog::level::info);
    return kestrel::run_app(argc, argv);
}
