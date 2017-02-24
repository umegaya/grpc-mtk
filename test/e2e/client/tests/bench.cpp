#include "bench.h"

void test_bench(mtk_conn_t c, test &t, test::testconn &conn) {
	auto done = t.latch();
	int count = 16, recv = 0;
	auto lock = std::unique_lock<std::mutex>(conn.mtx);
	for (int i = 0; i < count; i++) {
		PingRequest req;
		req.set_sent(mtk_time());
		RPC(c, Ping, req, ([&done, &count, &req, &conn, &recv](PingReply *rep, Error *err) {
			if (err != nullptr || req.sent() != rep->sent()) {
				count = 0;
			}
			recv++;
			conn.notify_cond();
		}));
		CONDWAIT(conn, lock, {});
	}
	//TRACE("Test_bench: cid={}, recv={}", mtk_conn_cid(c), recv);
	done(count > 0);
}
