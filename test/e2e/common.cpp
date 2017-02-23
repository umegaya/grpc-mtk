#include "common.h"

namespace mtktest {
bool test::launch(void *arg, mtk_cid_t cid, const char *p, size_t l) {
	TRACE("launch connection_id={}", cid);
	testconn *tc = (testconn *)arg;
	tc->t->threads_.push_back(std::thread([tc, cid] {
		auto t = tc->t;
		auto c = tc->c;
		if (cid != 0) {
			t->testfunc_(c, *t);
		} else {
			t->start();
			t->end(false); //failure			
		}
	}));
	return true;
}
bool test::run() {
	mtk_addr_t addr = {
		.host = addr_.c_str(),
		.cert = nullptr,
	};
	mtk_clconf_t conf = {
		.validate = nullptr,
		.payload_len = 0,
	};
	testconn conns[concurrency_];
	for (int i = 0; i < concurrency_; i++) {
		mtk_closure_init(&conf.on_connect, on_connect, &test::launch, &(conns[i]));
		conns[i].t = this;
		conns[i].c = mtk_connect(&addr, &conf);
	}
	while (!finished()) {
		for (int i = 0; i < concurrency_; i++) {
			mtk_conn_poll(conns[i].c);
		}	
	}
	for (auto &t : threads_) {
		t.join();
	}
	return success();
}
}

