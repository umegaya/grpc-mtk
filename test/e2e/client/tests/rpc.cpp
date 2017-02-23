#include "rpc.h"

void test_ping(mtk_conn_t conn, finish_cb done) {
	PingRequest req;
	req.set_sent(mtk_time());
	TRACE("test_ping: call RPC");
	RPC(conn, Ping, req, [done](PingRequest &req, PingReply &rep) {
		TRACE("test_ping: reply RPC");
		done(req.sent() == rep.sent());
	});
}

