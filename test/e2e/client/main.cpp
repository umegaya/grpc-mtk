#include "common.h"
#include <thread>
#include "tests/rpc.h"

using namespace mtktest;

void test_thread(mtk_conn_t c, test_finish_waiter &waiter) {
	test_ping(c, waiter.bind());
	//add more tests if neeeded.
}

int main(int argc, char *argv[]) {
	mtk_addr_t addr = {
		.host = "localhost:50051",
		.cert = nullptr,
	};
	mtk_clconf_t conf = {
		.validate = nullptr,
		.payload_len = 0,
	};
	auto c = mtk_connect(&addr, &conf);
	test_finish_waiter waiter;
	auto t = std::thread([c, &waiter] {
		test_thread(c, waiter);
	});
	while (waiter.finished()) {
		mtk_conn_poll(c);
	}
	t.join();
}
