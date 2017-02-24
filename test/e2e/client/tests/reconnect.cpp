#include "reconnect.h"

void test_reconnect(mtk_conn_t c, test &t) {
	auto done = t.latch();
	int count = 4;
	for (int i = 0; i < count; i++) {
		bool end = false;
		CloseRequest req;
		RPC(c, Close, req, ([&done, &end, &count](CloseReply *req, Error *err) {
			if (err != nullptr) {
				count = 0;
			}
			end = true;
		}));
		while (!end) {
			mtk_sleep(mtk_msec(5));
		}
		while (!mtk_conn_connected(c)) {
			mtk_sleep(mtk_msec(5));			
		}
	}
	done(count > 0);
}
