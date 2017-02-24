#if defined(_Atomic)
#include <stdatomic.h>
#define ATOMIC_INT std::atomic_int
#define ATOMIC_UINT64 std::atomic_ullong
#else
#include <atomic>
#define ATOMIC_INT std::atomic<int>
#define ATOMIC_UINT64 std::atomic<unsigned long long>

#endif
