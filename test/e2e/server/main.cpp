#include "common.h"
#include <thread>

using namespace mtktest;

mtk_result_t handler(mtk_svconn_t c, mtk_result_t r, const char *p, size_t pl) {
	switch (r) {
	HANDLE(c, Ping, [](mtk_svconn_t c, PingRequest &req, PingReply &rep) -> Error* {
		rep.set_sent(req.sent());
		return HANDLE_OK;
	});
	HANDLE(c, Close, [](mtk_svconn_t c, CloseRequest &req, CloseReply &rep) -> Error* {
		mtk_svconn_close(c);
		return HANDLE_OK;
	});
	HANDLE(c, Raise, [](mtk_svconn_t c, RaiseRequest &req, RaiseReply &rep) -> Error* {
		Error *err = new Error();
		err->set_code(req.code());
		err->set_message(req.message());
		return err;
	});
	HANDLE(c, Task, [](mtk_svconn_t c, TaskRequest &req, TaskReply &rep) -> Error* {
		TextTransferTask t;
		t.set_msgid(mtk_svconn_msgid(c));
		t.set_text(req.text());
		task_sender(c, MessageTypes::Task_TextTransfer, t);
		return HANDLE_PENDING_REPLY;
	});
	HANDLE(c, Notify, [](mtk_svconn_t c, NotifyRequest &req, NotifyReply &rep) -> Error* {
		TextNotify n;
		n.set_text(req.text());
		notify_sender(c, MessageTypes::Notify_Text, n);
		return HANDLE_OK;
	});
	HANDLE_TASK(c, TextTransfer, [](mtk_svconn_t c, TextTransferTask &t) {
		TaskReply rep;
		rep.set_text(t.text());
		reply_sender(c, t.msgid(), rep);
	});
	default:
		TRACE("unknown message: {}", r);
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
