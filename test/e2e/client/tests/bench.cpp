#include "bench.h"

void test_bench(mtk_conn_t c, test &t) {
	auto done = t.latch();
	int count = 16;
	for (int i = 0; i < count; i++) {
		bool end = false;
		PingRequest req;
		req.set_sent(mtk_time());
		RPC(c, Ping, req, ([&done, &end, &count, &req](PingReply *rep, Error *err) {
			if (err != nullptr || req.sent() != rep->sent()) {
				count = 0;
			}
			end = true;
		}));
		while (!end) {
			mtk_sleep(mtk_msec(5));
		}
	}

	done(count > 0);
}
