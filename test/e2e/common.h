#pragma once
#include <mtk.h>
#include <functional>
#include <codec.h>
#include <atomic_compat.h>
#if defined(CHECK)
#undef CHECK
#include <Catch/include/catch.hpp>
#endif
#include "./proto/test.pb.h"

namespace mtktest {
typedef std::function<void (std::string &)> finish_cb;
class test_finish_waiter {
	ATOMIC_INT running_;
public:
	test_finish_waiter() : running_(0) {}	
	void start() { running_++; }
	void end() { running_--; }
	bool finished() { return running_.load() == 0; }
	finish_cb bind() {
		return std::bind(&test_finish_waiter::end, this);
	}
	void join() {
		while (!finished()) {
			mtk_sleep(mtk_msec(50));
		}
	}
};
}

#define HANDLE(conn, type, handler) case mtktest::MessageTypes::type: { \
	type##Request req__; type##Reply rep__; \
	mtk::Codec::Unpack((const uint8_t *)p, pl, req__); \
	auto f = handler; \
	f(conn, req__, rep__); \
	char buff[rep__.ByteSize()]; \
	mtk::Codec::Pack(rep__, (uint8_t *)buff, rep__.ByteSize()); \
	mtk_svconn_send(conn, mtk_svconn_msgid(conn), buff, rep__.ByteSize()); \
}

#define RPC(conn, type, req, callback) { \
	\
}
