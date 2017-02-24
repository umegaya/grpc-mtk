#include "common.h"

namespace mtktest {
Error *HANDLE_PENDING_REPLY = (Error *)0x1;
Error *HANDLE_OK = (Error *)0x0;

bool test::launch(void *arg, mtk_cid_t cid, const char *p, size_t l) {
	testconn *tc = (testconn *)arg;
	if (tc->th.joinable()) {
		tc->notify_cond();
		return true; //already start
	}
	TRACE("launch connection_id={}", cid);
	tc->th = std::thread([tc, cid] {
		auto t = tc->t;
		auto c = tc->c;
		if (cid != 0) {
			t->testfunc_(c, *t, *tc);
		} else {
			t->start();
			t->end(false); //failure			
		}
	});
	tc->t->thread_start();
	return true;
}
mtk_time_t test::closed(void *arg, mtk_cid_t cid, long attempt) {
	testconn *tc = (testconn *)arg;
	tc->notify_cond();
	return mtk_sec(1);
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
	testconn *conns = new testconn[concurrency_];
	for (int i = 0; i < concurrency_; i++) {
		mtk_closure_init(&conf.on_connect, on_connect, &test::launch, &(conns[i]));
		mtk_closure_init(&conf.on_close, on_close, &test::closed, &(conns[i]));
		conns[i].t = this;
		conns[i].c = mtk_connect(&addr, &conf);
	}
	while (!finished()) {
		for (int i = 0; i < concurrency_; i++) {
			mtk_conn_poll(conns[i].c);
		}
		mtk_sleep(mtk_msec(5));
	}
	for (int i = 0; i < concurrency_; i++) {
		if (conns[i].th.joinable()) {
			conns[i].th.join();
		}
	}
	for (int i = 0; i < concurrency_; i++) {
		mtk_conn_close(conns[i].c);
	}
	delete []conns;
	return success();
}
}

