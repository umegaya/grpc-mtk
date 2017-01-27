#if defined(_Atomic)
#include <stdatomic.h>
#define ATOMIC_INT std::atomic_int
#else
#include <atomic>
#define ATOMIC_INT std::atomic<int>
#endif
