#include "logger.h"

namespace mtk {
std::shared_ptr<spdlog::logger> g_logger;
namespace logger {
void Initialize() {
    //TODO: send log over network (eg. fluentd)
    g_logger = spdlog::stderr_color_mt("stderr");
#if defined(DEBUG)
    g_logger->flush_on(spdlog::level::debug);
#else
    g_logger->flush_on(spdlog::level::info);
#endif
}
}
}
