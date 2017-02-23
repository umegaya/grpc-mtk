#pragma once

#include <spdlog/spdlog.h>

namespace mtk {
extern std::shared_ptr<::spdlog::logger> g_logger;
namespace logger {
	extern void Initialize();
    inline std::string Formatter(const char *fmt, fmt::ArgList args) { return fmt::format(fmt, args); }
    FMT_VARIADIC(std::string, Formatter, const char *);
    inline const char *Formatter(const char *fmt) { return fmt; }
}
}
