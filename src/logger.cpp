#include "logger.h"
#include <mutex>

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
	static void default_writer(const char *buf, size_t len) {
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
	void write(const std::string &body, const std::string &footer) {
		mtx_.lock();
#if defined(NO_LOG_WRITE_CALLBACK)
		fwrite(body.c_str(), 1, body.length(), stdout);
		fwrite(footer.c_str(), 1, footer.length(), stdout);
#else
		writer_(body.c_str(), body.length());
		writer_(footer.c_str(), footer.length());
#endif
		mtx_.unlock();
    }
}
}
