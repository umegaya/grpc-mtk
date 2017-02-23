#include "common.h"
#include <thread>
#include "tests/rpc.h"

using namespace mtktest;

void run_tests(mtk_conn_t c, test_finish_waiter &waiter) {
	test_ping(c, waiter.bind());
	//add more tests if neeeded.
}

static mtk_conn_t g_conn = nullptr;
static std::thread g_test_thread;
bool on_connect_callback(void *arg, mtk_cid_t cid, const char *p, size_t l) {
	TRACE("on_connect connection_id={}", cid);
	if (cid != 0) {
		//p, l represents additional payload (userdata, ...)
		g_test_thread = std::thread([arg] {
			TRACE("on_connect tests start");
			run_tests(g_conn, *(test_finish_waiter *)arg);
		});
		return true;
	} else {
		//p, l represents error
	}
	return false;
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
	mtk_log_init();
	mtk_closure_init(&conf.on_connect, on_connect, on_connect_callback, &waiter);
	g_conn = mtk_connect(&addr, &conf);
	while (!waiter.finished()) {
		mtk_conn_poll(g_conn);
	}
	g_test_thread.join();
}
