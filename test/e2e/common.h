#pragma once
#include <mtk.h>
#include <functional>
#include <vector>
#include <thread>
#include <string>
#include <codec.h>
#include <debug.h>
#include <atomic_compat.h>
#if defined(CHECK)
#undef CHECK
#endif
#include <Catch/include/catch.hpp>
#include "./proto/test.pb.h"

namespace mtktest {
class test {
public:
	typedef std::function<void (bool)> notifier;
	typedef std::function<void (mtk_conn_t, test &)> testfunc;
protected:
	struct testconn {
		mtk_conn_t c;
		test *t;
	};
	ATOMIC_INT running_;
	ATOMIC_INT result_;
	ATOMIC_INT start_;
	std::vector<std::thread> threads_;
	testfunc testfunc_;
	std::string addr_;
	int concurrency_;
public:
	test(const char *addr, testfunc tf, int cc = 1) : 
		running_(0), result_(0), start_(0), threads_(), testfunc_(tf), addr_(addr), concurrency_(cc) {}	
	void start() { start_.store(1); running_++; }
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
	bool success() const { return result_.load() == 1; }
	bool finished() const { return start_.load() != 0 && running_.load() == 0; }
	notifier done_signal() {
		start();
		return std::bind(&test::end, this, std::placeholders::_1);
	}
	bool run();
	static bool launch(void *, mtk_cid_t, const char *, size_t);
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
} break;



#define RPC(conn, type, req, callback) { \
	char buff[req.ByteSize()]; \
	mtk::Codec::Pack(req, (uint8_t *)buff, req.ByteSize()); \
	auto *pcc = new mtktest::closure_caller<type##Request, type##Reply>(); \
	pcc->cb = callback; \
	pcc->req = req; \
	mtk_closure_t clsr; \
	mtk_closure_init(&clsr, on_msg, pcc->call, pcc); \
	mtk_conn_send(conn, type, buff, req.ByteSize(), clsr); \
}
