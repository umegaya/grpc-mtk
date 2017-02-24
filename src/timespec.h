#pragma once

#include <cstdint>

namespace mtk {
	typedef uint64_t timespec_t;
	namespace clock {
		timespec_t now();
		timespec_t sleep(timespec_t dur);
		timespec_t pause(timespec_t dur);
		static inline constexpr timespec_t sec(uint64_t sec) { return sec * 1000 * 1000 * 1000; }
		static inline constexpr timespec_t msec(uint64_t msec) { return msec * 1000 * 1000; }
		static inline constexpr timespec_t usec(uint64_t usec) { return usec * 1000; }
		static inline constexpr timespec_t nsec(uint64_t nsec) { return nsec; }
	}
}
