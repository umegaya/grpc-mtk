#include "timespec.h"
#include <time.h>
#include <cerrno>

namespace mtk {
	namespace clock {
		static inline timespec_t to_timespec(struct timespec &ts) {
			return ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
		}
		static inline timespec_t rawsleep(timespec_t dur, bool ignore_intr) {
			int r; struct timespec ts, rs, *pts = &ts, *prs = &rs, *tmp;
			ts.tv_sec = dur / (1000 * 1000 * 1000);
			ts.tv_nsec = dur % (1000 * 1000 * 1000);
		resleep:
			//TRACE("start:%p %u(s) + %u(ns)\n", pts, pts->tv_sec, pts->tv_nsec);
			if (0 == (r = nanosleep(pts, prs))) {
				return 0;
			}
			//TRACE("left:%p %u(s) + %u(ns)\n", prs, prs->tv_sec, prs->tv_nsec);
			/* signal interrupt. keep on sleeping */
			if (r == -1 && errno == EINTR) {
				tmp = pts; pts = prs; prs = tmp;
				if (!ignore_intr) {
					goto resleep;
				}
			}
			return to_timespec(*prs);
		}
		timespec_t now() {
			struct timespec ts;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			return to_timespec(ts);
		}
		timespec_t sleep(timespec_t dur) {
			return rawsleep(dur, true);
		}
		timespec_t pause(timespec_t dur) {
			return rawsleep(dur, true);
		}
	}
}
