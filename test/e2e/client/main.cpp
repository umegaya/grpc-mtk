#include <mtk.h>
#include <thread>
#include "tests/ping.h"

std::thread s_test_thrd;
void test_thread(mtk_conn_t c) {

}

int main(int argc, char *argv[]) {
	mtk_addr_t addr = {
		.host = "localhost:50051",
		.cert = nullptr,
	};
	mtk_clconf_t conf = {
		.validate = nullptr,
		.payload_len = 0,
	}
	auto c = mtk_connect(&addr, &conf);
	s_test_thrd = std::move(std::thread([c] {
		test_thread(c);
	}))
	while (true) {
		mtk_conn_poll(c);
	}
}
