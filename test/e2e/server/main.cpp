#include "common.h"
#include <codec.h>
#include <MoodyCamel/concurrentqueue.h>
#include <thread>

using namespace mtktest;

moodycamel::ConcurrentQueue<LoginTask> g_login_queue;

static inline mtk_result_t handler_common(void *, mtk_svconn_t c, reply_dest *dst, mtk_result_t r, const char *p, mtk_size_t pl) {
	switch (r) {
	HANDLE(c, dst, Ping, [](mtk_svconn_t c, PingRequest &req, PingReply &rep) -> Error* {
		rep.set_sent(req.sent());
		return HANDLE_OK;
	});
	HANDLE(c, dst, Close, [dst](mtk_svconn_t c, CloseRequest &req, CloseReply &rep) -> Error* {
		if (c != nullptr) {
			mtk_svconn_close(c);
		} else {
			mtk_cid_close(dst->cid);
		}
		return HANDLE_OK;
	});
	HANDLE(c, dst, Raise, [](mtk_svconn_t c, RaiseRequest &req, RaiseReply &rep) -> Error* {
		Error *err = new Error();
		err->set_code(req.code());
		err->set_message(req.message());
		return err;
	});
	HANDLE(c, dst, Task, [dst](mtk_svconn_t c, TaskRequest &req, TaskReply &rep) -> Error* {
		TextTransferTask t;
		t.set_msgid(c != nullptr ? mtk_svconn_msgid(c) : dst->msgid);
		t.set_text(req.text());
		task_sender(c, dst, MessageTypes::Task_TextTransfer, t);
		return HANDLE_PENDING_REPLY;
	});
	HANDLE(c, dst, Notify, [dst](mtk_svconn_t c, NotifyRequest &req, NotifyReply &rep) -> Error* {
		TextNotify n;
		n.set_text(req.text());
		notify_sender(c, dst, MessageTypes::Notify_Text, n);
		return HANDLE_OK;
	});
	HANDLE_TASK(c, TextTransfer, [dst](mtk_svconn_t c, TextTransferTask &t) {
		TaskReply rep;
		rep.set_text(t.text());
		reply_sender(c, dst, t.msgid(), rep);
	});
	default:
		TRACE("unknown message: {}", r);
		ASSERT(false);
		exit(-1);
		break;
	}
	return 0;
}

mtk_result_t handler(void *a, mtk_svconn_t c, mtk_result_t r, const char *p, mtk_size_t pl) {
	return handler_common(a, c, nullptr, r, p, pl);
}

mtk_result_t queue_handler(reply_dest *dst, mtk_result_t r, const char *p, mtk_size_t pl) {
	return handler_common(nullptr, nullptr, dst, r, p, pl);
}

mtk_cid_t acceptor(void *arg, mtk_svconn_t c, mtk_msgid_t msgid, mtk_cid_t cid, 
					const char *p, mtk_size_t pl, char **rep, mtk_size_t *rep_len) {
	auto &seed = *(ATOMIC_UINT64 *)arg;
	ConnectPayload cp;
	mtk::Codec::Unpack((const uint8_t *)p, pl, cp);
	*rep_len = 0;
	if (cid != 0) {
		return cid;
	} else if (cp.login_mode() == ConnectPayload::Pending) {
		LoginTask lt;
		auto lcid = mtk_svconn_defer_login(c);
		lt.set_login_cid(lcid);
		lt.set_msgid(msgid);
		lt.set_use_pending(true);
		g_login_queue.enqueue(lt);
		return 0;
	} else if (cp.login_mode() == ConnectPayload::Failure) {
		mtk_svconn_close(c);
		return 0;
	} else {
		return ++seed;
	}
}

int main(int argc, char *argv[]) {
	mtk_log_init();

	ATOMIC_UINT64 id_seed;
	bool alive = true;
	mtk_server_t sv[2];
	mtk_addr_t addr[] = {
		{
			.host = "0.0.0.0:50051",
			.cert = nullptr,
		},
		{
			.host = "0.0.0.0:50052",
			.cert = nullptr,
		},
	};
	mtk_svconf_t conf[] = {
		{
			.exclusive = true,
			.n_worker = 4,
		},
		{
			.exclusive = false,
			.use_queue = true,
			.n_worker = 1,
		},
	};
	mtk_closure_init(&(conf[0].handler), on_svmsg, handler, nullptr);
	mtk_closure_init(&(conf[0].acceptor), on_accept, acceptor, &id_seed);
	mtk_closure_init(&(conf[1].handler), on_svmsg, handler, nullptr);
	mtk_closure_init(&(conf[1].acceptor), on_accept, acceptor, &id_seed);

	mtk_listen(&addr[1], &conf[1], &sv[1]);	

	auto q = mtk_server_queue(sv[1]);
	auto th1 = std::thread([q, &id_seed, &alive] {
		TRACE("sv event thread start");
		while (alive) {
			mtk_svevent_t *t;
			while (mtk_queue_pop(q, (void **)&t)) {
				if (t->lcid != 0) {	
					mtk_svconn_finish_login(t->lcid, t->cid != 0 ? t->cid : ++id_seed, t->msgid, t->data, t->datalen);
				} else {
					reply_dest rd = { t->cid, t->msgid };
					queue_handler(&rd, t->result, t->data, t->datalen);
				}
			}
			mtk_sleep(mtk_msec(10));
		}		
	});

	//pending login processing thread
	auto th2 = std::thread([&id_seed, &alive] {
		TRACE("login thread start");
		LoginTask lt;
		while (alive) {
			if (g_login_queue.try_dequeue(lt)) {
				ASSERT(lt.use_pending());
				TRACE("process pending login {} {}", lt.login_cid(), lt.msgid());
				mtk_svconn_finish_login(lt.login_cid(), ++id_seed, lt.msgid(), nullptr, 0);
			}
			mtk_sleep(mtk_msec(10));
		}
	});

	mtk_listen(&addr[0], &conf[0], &sv[0]);

	alive = false;
	th1.join();
	th2.join();
}
