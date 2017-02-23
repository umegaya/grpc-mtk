#include "common.h"
#include "tests/rpc.h"

using namespace mtktest;

void test_rpc(mtk_conn_t c, test &t) {
	test_ping(c, t.done_signal());
}
void test_bench(mtk_conn_t c, test &t) {
	auto done = t.done_signal();
	int count = 16;
	for (int i = 0; i < count; i++) {
		bool end = false;
		PingRequest req;
		req.set_sent(mtk_time());
		RPC(c, Ping, req, ([&done, &end, &count](PingRequest &req, PingReply &rep) {
			if (req.sent() != rep.sent()) {
				done(false);
				count = 0;
			}
			end = true;
		}));
		while (!end) {
			mtk_sleep(mtk_msec(5));
		}
	}
	done(true);
}
void test_reconnection(mtk_conn_t c, test &t) {
}
int main(int argc, char *argv[]) {
	mtk_log_init();

	test t2("localhost:50051", test_bench, 256);
	t2.run();

	test t("localhost:50051", test_rpc);
	t.run();

	//test t3("localhost:50051", test_reconnection);
	//t3.run();
}

