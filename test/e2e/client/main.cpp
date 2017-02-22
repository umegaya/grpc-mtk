#include "common.h"
#include <thread>
#include "tests/rpc.h"

using namespace mtktest;

void run_tests(mtk_conn_t c, test_finish_waiter &waiter) {
	test_ping(c, waiter.bind());
	//add more tests if neeeded.
}

mtk_conn_t g_conn = nullptr;
void on_connect(void *arg, mtk_result_t r, const char *p, size_t l) {
	auto t = std::thread([arg] {
		run_tests(g_conn, *(test_finish_waiter *)arg);
	});
}

int main(int argc, char *argv[]) {
	test_finish_waiter waiter;
	mtk_addr_t addr = {
		.host = "localhost:50051",
		.cert = nullptr,
	};
	mtk_clconf_t conf = {
		.validate = nullptr,
		.payload_len = 0,
	};
	mtk_closure_new(on_connect, &waiter, &conf.on_connect);
	g_conn = mtk_connect(&addr, &conf);
	while (waiter.finished()) {
		mtk_conn_poll(g_conn);
	}
}
