#include "logger.h"
#include <mutex>
#if defined(__MTK_OSX__)
#include <MoodyCamel/concurrentqueue.h>
#endif

namespace mtk {
namespace logger {
	const std::string log_level_[level::max] = {
	    "trace",
	    "debug",
	    "info",
	    "warn",
	    "error",
	    "fatal",
	    "report",
	};
	static void default_writer(const char *buf, size_t len, bool) {
		fwrite(buf, 1, len, stdout);
	}
	static writer_cb_t writer_ = default_writer;
	static std::string name_ = "mtk";
	static std::mutex mtx_;
    void configure(writer_cb_t cb, const std::string &name) {
    	if (cb != nullptr) {
			writer_ = cb;
		}
		if (name.length() > 0) {
			name_ = name;
		}
	}
	const std::string &svname() { return name_; }

#if defined(__MTK_OSX__) && !defined(NO_LOG_WRITE_CALLBACK)
	static moodycamel::ConcurrentQueue<std::string> s_logs;
    void flush_from_main_thread() {
//		printf("flush_from_main_thread");
    	std::string str;
    	while (s_logs.try_dequeue(str)) {
    		writer_(str.c_str(), str.length(), true);
    	}
    }
#else
    void flush_from_main_thread() {
	}
#endif

	void write(const std::string &body, const std::string &footer) {
		mtx_.lock();
#if defined(NO_LOG_WRITE_CALLBACK)
		fwrite(body.c_str(), 1, body.length(), stdout);
		fwrite(footer.c_str(), 1, footer.length(), stdout);
#elif defined(__MTK_OSX__)
		if (writer_ != default_writer) {
			s_logs.enqueue(body + footer);
		} else {
			writer_(body.c_str(), body.length(), false);
			writer_(footer.c_str(), footer.length(), true);
		}
#else
		writer_(body.c_str(), body.length(), false);
		writer_(footer.c_str(), footer.length(), true);
#endif
		mtx_.unlock();
    }


	void write(const std::string &body) {
		mtx_.lock();
#if defined(NO_LOG_WRITE_CALLBACK)
		fwrite(body.c_str(), 1, body.length(), stdout);
#elif defined(__MTK_OSX__)
		if (writer_ != default_writer) {
			s_logs.enqueue(body);
		} else {
			writer_(body.c_str(), body.length(), true);			
		}
#else
		writer_(body.c_str(), body.length(), true);
#endif
		mtx_.unlock();
    }
}
}
