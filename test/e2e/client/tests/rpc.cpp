#include "rpc.h"

static void test_ping(mtk_conn_t conn, test::notifier done) {
	PingRequest req;
	auto now = mtk_time();
	req.set_sent(now);
	TRACE("test_ping: call RPC");
	RPC(conn, Ping, req, ([done, now](PingReply *rep, Error *err) {
		TRACE("test_ping: reply RPC");
		done(err == nullptr && now == rep->sent());
	}));
}

static void test_raise(mtk_conn_t conn, test::notifier done) {
	const int code = -999;
	const std::string msg = "test error";
	RaiseRequest req;
	req.set_code(code);
	req.set_message(msg);
	TRACE("test_raise: call RPC");
	RPC(conn, Raise, req, ([done, code, msg](RaiseReply *rep, Error *err) {
		TRACE("test_raise: reply RPC");
		done(err != nullptr && 
			err->code() == code && err->message() == msg);
	}));	
}

static void test_task(mtk_conn_t conn, test::notifier done) {
	TaskRequest req;
	const std::string text = "process this task plz";
	req.set_text(text);
	TRACE("test_task: call RPC");
	RPC(conn, Task, req, ([done, text](TaskReply *rep, Error *err) {
		TRACE("test_task: reply RPC");
		done(err == nullptr && rep->text() == text);
	}));
}

static void test_notify(mtk_conn_t conn, test::notifier done) {
	const std::string text = "notify this plz";
	notify_closure_caller<TextNotify> *ppcc = nullptr;
	WATCH_NOTIFY(conn, Text, Notify_Text, ([done, text, ppcc](MessageTypes t, TextNotify &n) {
		TRACE("test_notify: notified");		
		done(n.text() == text && t == MessageTypes::Notify_Text);
		delete ppcc;
	}), &ppcc);
	NotifyRequest req;
	req.set_text(text);
	TRACE("test_notify: call RPC");
	RPC(conn, Notify, req, ([](NotifyReply *rep, Error *err) {}));
}

void test_rpc(mtk_conn_t c, test &t, test::testconn &conn) {
	test_ping(c, t.latch());
	test_raise(c, t.latch());
	test_task(c, t.latch());
	test_notify(c, t.latch());
}
