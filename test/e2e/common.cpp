#include "common.h"

namespace mtktest {
Error *HANDLE_PENDING_REPLY = (Error *)0x1;
Error *HANDLE_OK = (Error *)0x0;

bool test::launch(void *arg, mtk_cid_t cid, const char *p, mtk_size_t l) {
	testconn *tc = (testconn *)arg;
	if (tc->th.joinable()) {
		tc->should_signal = true;
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
	tc->should_signal = true;
	return mtk_sec(1);
}
bool test::run(ConnectPayload::LoginMode login_mode, mtk_time_t timeout) {
	ConnectPayload p;
	p.set_login_mode(login_mode);
	char buff[p.ByteSize()];
	mtk::Codec::Pack(p, (uint8_t *)buff, p.ByteSize());
	mtk_addr_t addr = {
		.host = addr_.c_str(),
		.cert = nullptr,
	};
	mtk_clconf_t conf = {
		.on_ready = mtk_closure_nop,
		.payload = (char *)buff,
		.payload_len = static_cast<mtk_size_t>(p.ByteSize()),
	};
	testconn *conns = new testconn[concurrency_];
	for (int i = 0; i < concurrency_; i++) {
		mtk_closure_init(&conf.on_connect, on_connect, &test::launch, &(conns[i]));
		mtk_closure_init(&conf.on_close, on_close, &test::closed, &(conns[i]));
		conns[i].t = this;
		conns[i].c = mtk_connect(&addr, &conf);
		conns[i].should_signal = false;
	}
	mtk_time_t end = mtk_time() + timeout;
	while (!finished() && (timeout == 0 || mtk_time() < end)) {
		for (int i = 0; i < concurrency_; i++) {
			mtk_conn_poll(conns[i].c);
			if (conns[i].should_signal) {
				conns[i].should_signal = false;
				conns[i].notify_cond();
			}
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
	if (timeout > 0) {
		return true;
	}
	return is_success();
}
}

