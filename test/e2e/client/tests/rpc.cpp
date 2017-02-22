#include "rpc.h"

void test_ping(mtk_conn_t conn, finish_cb done) {
	PingRequest req;
	req.set_sent(mtk_time());
	RPC(conn, Ping, req, [done](PingRequest &req, PingReply &rep) {
		done(req.sent() == rep.sent());
	});
}

