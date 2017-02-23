#include "common.h"
#include <thread>

using namespace mtktest;

mtk_result_t handler(mtk_svconn_t c, mtk_result_t r, const char *p, size_t pl) {
	switch (r) {
	HANDLE(c, Ping, [](mtk_svconn_t c, PingRequest &req, PingReply &rep) {
		//TRACE("handle Ping {}", mtk_svconn_cid(c));
		rep.set_sent(req.sent());
	});
	default:
		exit(-1);
		break;
	}
	return 0;
}

ATOMIC_UINT64 g_id_seed;
mtk_cid_t acceptor(mtk_svconn_t c, mtk_cid_t cid, const char *p, size_t pl, char **rep, size_t *rep_len) {
	*rep_len = 0;
	if (cid != 0) {
		return cid;
	} else {
		return ++g_id_seed;
	}
}

int main(int argc, char *argv[]) {
	mtk_log_init();

	mtk_addr_t addr = {
		.host = "0.0.0.0:50051",
		.cert = nullptr,
	};
	mtk_svconf_t conf = {
		.exclusive = true,
		.thread = {
			.n_reader = 64,
			.n_writer = 16,
		},
		.handler = handler,
		.acceptor = acceptor,
	};
	mtk_listen(&addr, &conf);
}
