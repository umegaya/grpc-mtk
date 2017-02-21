#pragma once

#if defined(DEBUG)
#include <cassert>
#define ASSERT assert
#else
#define ASSERT(...)
#endif