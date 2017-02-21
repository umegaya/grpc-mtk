#include "common.h"
#include <thread>

using namespace mtktest;

mtk_result_t handler(mtk_svconn_t c, mtk_result_t r, const char *p, size_t pl) {
	switch (r) {
	HANDLE(c, Ping, [](mtk_svconn_t c, PingRequest &req, PingReply &rep) {
		rep.set_sent(req.sent());
	});
	default:
		exit(-1);
		break;
	}
	return 0;
}

int main(int argc, char *argv[]) {
	mtk_addr_t addr = {
		.host = "0.0.0.0:50051",
		.cert = nullptr,
	};
	mtk_svconf_t conf = {
		.exclusive = true,
		.thread = {
			.n_reader = 2,
			.n_writer = 2,
		},
		.handler = handler,
	};
	mtk_listen(&addr, &conf);
}
