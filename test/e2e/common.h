#pragma once
#include <mtk.h>
#include <functional>
#include <codec.h>
#include <atomic_compat.h>
#if defined(CHECK)
#undef CHECK
#endif
#include <Catch/include/catch.hpp>
#include "./proto/test.pb.h"

namespace mtktest {
typedef std::function<void (bool)> finish_cb;
class test_finish_waiter {
	ATOMIC_INT running_;
	ATOMIC_INT result_;
public:
	test_finish_waiter() : running_(0), result_(0) {}	
	void start() { running_++; }
	void end(bool success) { 
		running_--; 
		while (true) {
            int32_t expect = result_.load();
            if (expect < 0) {
            	break;
            }
            int32_t desired = success ? 1 : -1;
            if (atomic_compare_exchange_weak(&result_, &expect, desired)) {
                return;
            }
        }
	}
	bool finished() { return running_.load() == 0; }
	finish_cb bind() {
		return std::bind(&test_finish_waiter::end, this, std::placeholders::_1);
	}
	void join() {
		while (!finished()) {
			mtk_sleep(mtk_msec(50));
		}
	}
};
template <class REQ, class REP>
class closure_caller {
public:
	std::function<void (REQ &, REP &)> cb;
	REQ req;
public:
	closure_caller() : cb(), req() {}
	static void call(void *arg, mtk_result_t r, const char *p, size_t l) {
		auto pcc = (closure_caller *)arg;
		REP rep;
		mtk::Codec::Unpack((const uint8_t *)p, l, rep);
		pcc->cb(pcc->req, rep);
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
	char buff[req.ByteSize()]; \
	mtk::Codec::Pack(req, (uint8_t *)buff, req.ByteSize()); \
	auto *pcc = new mtktest::closure_caller<type##Request, type##Reply>(); \
	pcc->cb = callback; \
	pcc->req = req; \
	mtk_closure_t clsr; \
	mtk_closure_new(pcc->call, pcc, &clsr); \
	mtk_conn_send(conn, type, buff, req.ByteSize(), clsr); \
}
