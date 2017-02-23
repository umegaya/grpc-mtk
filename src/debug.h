#pragma once

#if defined(DEBUG)
#include <cassert>
#define ASSERT assert
#else
#define ASSERT(...)
#endif

#if defined(DEBUG)
#include "logger.h"
#define TRACE(...) ::mtk::g_logger->info(__VA_ARGS__)
#else
#define TRACE(...)
#endif
