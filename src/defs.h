#pragma once

#include "platform.h"

#if defined(_Atomic)
#include <stdatomic.h>
#define ATOMIC_INT std::atomic_int
#define ATOMIC_UINT64 std::atomic_ullong
#else
#include <atomic>
#define ATOMIC_INT std::atomic<int>
#define ATOMIC_UINT64 std::atomic<unsigned long long>
#endif
#define mtk_likely(x)       __builtin_expect(!!(x),1)
#define mtk_unlikely(x)     __builtin_expect(!!(x),0)
