#pragma once

namespace mtk {
	typedef uint64_t timespec_t;
	namespace time {
		static timespec_t clock();
		static inline timespec_t sec(uint32_t sec) { return sec * 1000 * 1000 * 1000; }
		static inline timespec_t msec(uint32_t msec) { return msec * 1000 * 1000; }
		static inline timespec_t usec(uint32_t usec) { return usec * 1000; }
	}
}
