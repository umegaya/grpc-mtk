#pragma once

#if defined(DEBUG)
#include <cassert>
#define ASSERT assert
#else
#define ASSERT(...)
#endif

#if defined(DEBUG)
#define TRACE(...) LOG(trace, __VA_ARGS__)
#else
#define TRACE(...)
#endif
