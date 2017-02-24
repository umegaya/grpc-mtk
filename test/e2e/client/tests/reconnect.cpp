#include "reconnect.h"

void test_reconnect(mtk_conn_t c, test &t, test::testconn &conn) {
	auto done = t.latch();
	int count = 4;
	std::mutex mtx;
	std::condition_variable cond;
	auto lock = std::unique_lock<std::mutex>(conn.mtx);
	for (int i = 0; i < count; i++) {
		CloseRequest req;
		RPC(c, Close, req, ([&count](CloseReply *req, Error *err) {
			if (err != nullptr) {
				count = 0;
			}
		}));
		CONDWAIT(conn, lock, {
			if (mtk_conn_connected(c)) {
				TRACE("connection should be closed");
				done(false);
				return;
			}
		});
		CONDWAIT(conn, lock, {
			if (!mtk_conn_connected(c)) {
				TRACE("connection should be opened");
				done(false);
				return;
			}
		});
	}
	done(count > 0);
}
