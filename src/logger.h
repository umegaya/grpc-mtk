#pragma once

#include <spdlog/spdlog.h>
#include "timespec.h"

namespace mtk {
namespace logger {
    class level {
    public:
    	enum def {
	        trace,
	        debug,
	        info,
	        warn,
	        error,
	        fatal,            
	        report,
	        max,
	    };
	};
    extern const std::string log_level_[level::max];
    typedef void (*writer_cb_t)(const char *, size_t, bool);
    void configure(writer_cb_t cb, const std::string &name);
    const std::string &svname();
    void write(const std::string &body, const std::string &footer);
    void write(const std::string &body);

    template <typename... Args> inline void log(level::def lv, const char *fmt, fmt::ArgList args) {
    	if (lv <= level::debug) {
	        std::string body = fmt::format(fmt, args);
	        write(body + "\n");
			return;    		
    	}
        long sec, nsec;
        clock::now(sec, nsec);
        std::string footer = fmt::format(",sv_:{},ts_:{}.{},lv_:{}\n", svname(),sec,nsec,log_level_[lv]);
        std::string body = fmt::format(fmt, args);
        write(body, footer);
    }
    inline void log_no_arg(level::def lv, const std::string &body) {
        if (lv <= level::debug) {
            write(body + "\n");
            return;         
        }
        long sec, nsec;
        clock::now(sec, nsec);
        std::string footer = fmt::format(",sv_:{},ts_:{}.{},lv_:{}\n", svname(),sec,nsec,log_level_[lv]);
        write(body, footer);        
    }
	FMT_VARIADIC(void, log, level::def, const char *);	
    template <typename... Args> inline void trace(const char *fmt, const Args&... args) { log(level::trace, fmt, args...); }
    template <typename... Args> inline void debug(const char *fmt, const Args&... args) { log(level::debug, fmt, args...); }
    template <typename... Args> inline void info(const char *fmt, const Args&... args) { log(level::info, fmt, args...); }
    template <typename... Args> inline void warn(const char *fmt, const Args&... args) { log(level::warn, fmt, args...); }
    template <typename... Args> inline void error(const char *fmt, const Args&... args) { log(level::error, fmt, args...); }
    template <typename... Args> inline void fatal(const char *fmt, const Args&... args) { log(level::fatal, fmt, args...); }
    template <typename... Args> inline void report(const char *fmt, const Args&... args) { log(level::report, fmt, args...); }

    inline std::string Format(const char *fmt, fmt::ArgList args) { return fmt::format(fmt, args); }
    FMT_VARIADIC(std::string, Format, const char *);
    inline const char *Format(const char *fmt) { return fmt; }
    void flush_from_main_thread();
}
}

#define LOG(level__, ...) { ::mtk::logger::level__(__VA_ARGS__); } 