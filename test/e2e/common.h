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
	notifier latch() {
		start();
		return std::bind(&test::end, this, std::placeholders::_1);
	}
	bool run();
	static bool launch(void *, mtk_cid_t, const char *, size_t);
};



template <class REP>
class reply_closure_caller {
public:
	std::function<void (REP *, Error *)> cb;
public:
	reply_closure_caller() : cb() {}
	mtk_closure_t closure() {
		mtk_closure_t clsr;
		mtk_closure_init(&clsr, on_msg, reply_closure_caller::call, this);
		return clsr;
	}
	static void call(void *arg, mtk_result_t r, const char *p, size_t l) {
		auto pcc = (reply_closure_caller *)arg;
		if (r >= 0) {
			REP rep;
			mtk::Codec::Unpack((const uint8_t *)p, l, rep);
			pcc->cb(&rep, nullptr);
		} else {
			Error err;
			mtk::Codec::Unpack((const uint8_t *)p, l, err);
			pcc->cb(nullptr, &err);
		}
		delete pcc;
	}
};

template <class NOTIFY>
class notify_closure_caller {
public:
	std::function<void (NOTIFY &)> cb;
public:
	notify_closure_caller() : cb() {}
	mtk_closure_t closure() {
		mtk_closure_t clsr;
		mtk_closure_init(&clsr, on_msg, notify_closure_caller::call, this);
		return clsr;
	}
	static void call(void *arg, mtk_result_t r, const char *p, size_t l) {
		auto pcc = (notify_closure_caller *)arg;
		NOTIFY n;
		mtk::Codec::Unpack((const uint8_t *)p, l, n);
		pcc->cb(n);
	}
};

extern Error *HANDLE_PENDING_REPLY;
extern Error *HANDLE_OK;

template <class TASK>
void task_sender(mtk_svconn_t conn, uint32_t type, TASK &t) {
	char buff[t.ByteSize()];
	mtk::Codec::Pack(t, (uint8_t *)buff, t.ByteSize());	
	mtk_svconn_task(conn, type, buff, t.ByteSize());
}
template <class REPLY>
void reply_sender(mtk_svconn_t conn, mtk_msgid_t msgid, REPLY &rep) {
	char buff[rep.ByteSize()];
	mtk::Codec::Pack(rep, (uint8_t *)buff, rep.ByteSize());	
	mtk_svconn_send(conn, msgid, buff, rep.ByteSize());
}
template <class NOTIFY>
void notify_sender(mtk_svconn_t conn, uint32_t type, NOTIFY &n) {
	char buff[n.ByteSize()];
	mtk::Codec::Pack(n, (uint8_t *)buff, n.ByteSize());	
	mtk_svconn_notify(conn, type, buff, n.ByteSize());
}
}



#define HANDLE(conn, type, handler) case mtktest::MessageTypes::type: { \
	type##Request req__; type##Reply rep__; \
	mtk::Codec::Unpack((const uint8_t *)p, pl, req__); \
	auto f = handler; \
	Error *e = f(conn, req__, rep__); \
	if (e == nullptr) { \
		char buff[rep__.ByteSize()]; \
		mtk::Codec::Pack(rep__, (uint8_t *)buff, rep__.ByteSize()); \
		mtk_svconn_send(conn, mtk_svconn_msgid(conn), buff, rep__.ByteSize()); \
	} else if (e != HANDLE_PENDING_REPLY) { \
		char buff[e->ByteSize()]; \
		mtk::Codec::Pack(*e, (uint8_t *)buff, e->ByteSize()); \
		mtk_svconn_error(conn, mtk_svconn_msgid(conn), buff, e->ByteSize()); \
	} \
} break;

#define HANDLE_TASK(conn, type, handler) case mtktest::MessageTypes::Task_##type: { \
	type##Task task__; \
	mtk::Codec::Unpack((const uint8_t *)p, pl, task__); \
	auto f = handler; \
	f(conn, task__); \
} break;



#define RPC(conn, type, req, callback) { \
	char buff[req.ByteSize()]; \
	mtk::Codec::Pack(req, (uint8_t *)buff, req.ByteSize()); \
	auto *pcc = new mtktest::reply_closure_caller<type##Reply>(); \
	pcc->cb = callback; \
	mtk_conn_send(conn, type, buff, req.ByteSize(), pcc->closure()); \
}

#define WATCH_NOTIFY(conn, type, callback, ppcc) { \
	auto *pcc = new mtktest::notify_closure_caller<type##Notify>(); \
	pcc->cb = callback; \
	*ppcc = pcc; \
	mtk_conn_watch(conn, MessageTypes::Notify_##type, pcc->closure()); \
}


#define ALERT_AND_EXIT(msg) { \
	TRACE("test failure: {}", msg); \
	exit(-1); \
}



