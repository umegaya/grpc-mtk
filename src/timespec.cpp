#include "timespec.h"
#include <time.h>

namespace mtk {
	namespace time {
		timespec_t clock() {
			struct timespec ts;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			return ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
		}
	}
}
