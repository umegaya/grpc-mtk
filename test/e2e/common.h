#pragma once
#include <mtk.h>
#include <functional>
#include <vector>
#include <thread>
#include <string>
#include <mutex>
#include <condition_variable>
#include <codec.h>
#include <logger.h>
#include <debug.h>
#include <defs.h>
#if defined(CHECK)
#undef CHECK
#endif
#include "./proto/test.pb.h"

namespace mtktest {
class test {
public:
	struct testconn {
		mtk_conn_t c;
		test *t;
		std::thread th;
		std::mutex mtx;
		std::condition_variable cond;
		bool should_signal;
		void notify_cond() {
			std::unique_lock<std::mutex> lock(mtx);
			cond.notify_one();
		}
	};
	typedef std::function<void (bool)> notifier;
	typedef std::function<void (mtk_conn_t, test &, testconn &conn)> testfunc;
protected:
	ATOMIC_INT running_;
	ATOMIC_INT result_;
	ATOMIC_INT test_start_;
	ATOMIC_INT thread_start_;
	testfunc testfunc_;
	std::string addr_;
	int concurrency_;
public:
	test(const char *addr, testfunc tf, int cc = 1) : 
		running_(0), result_(0), test_start_(0), thread_start_(0), testfunc_(tf), addr_(addr), concurrency_(cc) {}	
	void thread_start() { thread_start_++; }
	void start() { test_start_++; running_++; }
	void end(bool ok) { 
		ASSERT(ok);
		running_--; 
		while (true) {
            int32_t expect = result_.load();
            if (expect < 0) {
            	break;
            }
            int32_t desired = ok ? 1 : -1;
            if (atomic_compare_exchange_weak(&result_, &expect, desired)) {
                return;
            }
        }
	}
	bool is_success() const { return result_.load() == 1; }
	bool finished() const { return test_start_.load() > 0 && thread_start_ == concurrency_ && running_.load() == 0; }
	notifier latch() {
		start();
		return std::bind(&test::end, this, std::placeholders::_1);
	}
	bool run(ConnectPayload::LoginMode login_mode = ConnectPayload::Invalid, mtk_time_t timeout = 0);
	static bool launch(void *, mtk_cid_t, const char *, mtk_size_t);
	static mtk_time_t closed(void *arg, mtk_cid_t cid, long attempt);
	static mtk_cid_t on_start(void *arg, mtk_slice_t s);
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
	static void call(void *arg, mtk_result_t r, const char *p, mtk_size_t l) {
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
	std::function<void (MessageTypes t, NOTIFY &)> cb;
	MessageTypes type_id;
public:
	notify_closure_caller(MessageTypes t) : cb(), type_id(t) {}
	mtk_closure_t closure() {
		mtk_closure_t clsr;
		mtk_closure_init(&clsr, on_msg, notify_closure_caller::call, this);
		return clsr;
	}
	static void call(void *arg, mtk_result_t r, const char *p, mtk_size_t l) {
		auto pcc = (notify_closure_caller *)arg;
		NOTIFY n;
		mtk::Codec::Unpack((const uint8_t *)p, l, n);
		pcc->cb(pcc->type_id, n);
	}
};

struct reply_dest {
	mtk_cid_t cid;
	mtk_msgid_t msgid;
};

extern Error *HANDLE_PENDING_REPLY;
extern Error *HANDLE_OK;

template <class TASK>
void task_sender(mtk_server_t sv, mtk_svconn_t conn, reply_dest *dst, uint32_t type, TASK &t) {
	char buff[t.ByteSize()];
	mtk::Codec::Pack(t, (uint8_t *)buff, t.ByteSize());	
	if (conn != nullptr) {
		mtk_svconn_task(conn, type, buff, t.ByteSize());
	} else {
		mtk_cid_task(sv, dst->cid, type, buff, t.ByteSize());
	}
}
template <class REPLY>
void reply_sender(mtk_server_t sv, mtk_svconn_t conn, reply_dest *dst, mtk_msgid_t msgid, REPLY &rep) {
	char buff[rep.ByteSize()];
	mtk::Codec::Pack(rep, (uint8_t *)buff, rep.ByteSize());	
	if (conn != nullptr) {
		mtk_svconn_send(conn, msgid, buff, rep.ByteSize());
	} else {
		mtk_cid_send(sv, dst->cid, msgid, buff, rep.ByteSize());
	}
}
template <class NOTIFY>
void notify_sender(mtk_server_t sv, mtk_svconn_t conn, reply_dest *dst, uint32_t type, NOTIFY &n) {
	char buff[n.ByteSize()];
	mtk::Codec::Pack(n, (uint8_t *)buff, n.ByteSize());	
	if (conn != nullptr) {
		mtk_svconn_notify(conn, type, buff, n.ByteSize());
	} else {
		mtk_cid_notify(sv, dst->cid, type, buff, n.ByteSize());		
	}
}
}



#define HANDLE(sv, conn, dest, type, handler) case mtktest::MessageTypes::type: { \
	type##Request req__; type##Reply rep__; \
	mtk::Codec::Unpack((const uint8_t *)p, pl, req__); \
	auto f = handler; \
	Error *e = f(conn, req__, rep__); \
	/*TRACE("{}: err = {}", #type, (void *)e);*/ \
	if (e == nullptr) { \
		char buff[rep__.ByteSize()]; \
		mtk::Codec::Pack(rep__, (uint8_t *)buff, rep__.ByteSize()); \
		if (conn == nullptr) { \
			mtk_cid_send(sv, dest->cid, dest->msgid, buff, rep__.ByteSize()); \
		} else { \
			mtk_svconn_send(conn, mtk_svconn_msgid(conn), buff, rep__.ByteSize()); \
		} \
	} else if (e != HANDLE_PENDING_REPLY) { \
		char buff[e->ByteSize()]; \
		mtk::Codec::Pack(*e, (uint8_t *)buff, e->ByteSize()); \
		if (conn == nullptr) { \
			mtk_cid_error(sv, dest->cid, dest->msgid, buff, e->ByteSize()); \
		} else { \
			mtk_svconn_error(conn, mtk_svconn_msgid(conn), buff, e->ByteSize()); \
		} \
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

#define WATCH_NOTIFY(conn, type, type_id, callback, ppcc) { \
	auto *pcc = new mtktest::notify_closure_caller<type##Notify>(MessageTypes::type_id); \
	pcc->cb = callback; \
	*ppcc = pcc; \
	mtk_conn_watch(conn, pcc->closure()); \
}


#define ALERT_AND_EXIT(msg) { \
	TRACE("test failure: {}", msg); \
	exit(-1); \
}

#define CONDWAIT(conn, lock, on_awake) { \
	conn.cond.wait(lock); \
	on_awake; \
}

