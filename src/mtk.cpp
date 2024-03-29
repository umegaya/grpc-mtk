#include "mtk.h"
#include "rpc.h"
#include "worker.h"
#include "conn.h"
#include "server.h"
#include "codec.h"
#include "http.h"
#include "timespec.h"
#include <grpc++/grpc++.h>
#include <thread>

using namespace mtk;

static_assert(offsetof(mtk_svevent_t, data) == 28, "mtk_svevent_t offset illegal");

/******* internal bridge *******/
/* worker handler (callback) */
class FunctionHandler : public IHandler {
protected:
	mtk_closure_t handler_, acceptor_, closer_;
public:
	FunctionHandler() : handler_(mtk_closure_nop), acceptor_(mtk_closure_nop), closer_(mtk_closure_nop) {}
	FunctionHandler(mtk_closure_t handler, mtk_closure_t acceptor, mtk_closure_t closer) : 
		handler_(handler), acceptor_(acceptor), closer_(closer) {}
	virtual ~FunctionHandler() {}
	//implements IHandler
	grpc::Status Handle(Conn *c, Request &req) {
		return mtk_closure_call(&handler_, on_svmsg, c, req.type(), req.payload().c_str(), req.payload().length()) >= 0 ? 
			grpc::Status::OK : grpc::Status::CANCELLED;
	}
	void Close(Conn *c) {
    	if (mtk_closure_valid(&closer_)) {
    		mtk_closure_call(&closer_, on_svclose, c);
    	}		
	}
	mtk_cid_t Login(Conn *c, Request &req, MemSlice &s) {
		SystemPayload::Connect creq;
		if (Codec::Unpack((const uint8_t *)req.payload().c_str(), req.payload().length(), creq) < 0) {
			return 0;
		}
		return mtk_closure_call(&acceptor_, on_accept, c, req.msgid(), 
										creq.id(), creq.payload().c_str(), creq.payload().length(), &s);
	}
	Conn *NewConn(Worker *worker, IHandler *handler) {
		return new Conn(worker, handler);
	}
};

/* worker handler (queue) */
class QueueReadHandler : public FunctionHandler {
protected:
	mtk_queue_t queue_;
public:
	QueueReadHandler() : FunctionHandler() {
		queue_ = mtk_queue_create(_DestroyEvent);
		mtk_closure_init(&handler_, on_svmsg, &_OnRecv, queue_);
		mtk_closure_init(&acceptor_, on_accept, &_OnAccept, queue_);
		mtk_closure_init(&closer_, on_svclose, &_OnClose, queue_);
	}
	virtual ~QueueReadHandler() {}
	mtk_queue_t Queue() { return queue_; }
protected:
	static mtk_result_t _OnRecv(void *arg, mtk_svconn_t c, mtk_result_t r, const char *p, mtk_size_t l) {
		mtk_queue_t q = ((mtk_queue_t)arg);
		//receiver send reply by using mtk_cid_send
		mtk_svevent_t *ev = (mtk_svevent_t *)std::malloc(sizeof(mtk_svevent_t) + l);
		ev->lcid = 0;
		ev->cid = mtk_svconn_cid(c);
		ev->msgid = mtk_svconn_msgid(c);
		ev->result = r;
		ev->datalen = l;
		std::memcpy(ev->data, p, l);
		mtk_queue_push(q, ev);
		return 0;
	}
	static void _OnClose(void *arg, mtk_svconn_t c) {
		mtk_queue_t q = ((mtk_queue_t)arg);
		//receiver send reply by using mtk_cid_send
		mtk_svevent_t *ev = (mtk_svevent_t *)std::malloc(sizeof(mtk_svevent_t));
		ev->lcid = 0;
		ev->cid = mtk_svconn_cid(c);
		ev->msgid = 0;
		ev->result = 0;
		ev->datalen = 0;
		mtk_queue_push(q, ev);
	}
	static mtk_cid_t _OnAccept(void *arg, mtk_svconn_t c, mtk_msgid_t msgid, mtk_cid_t cid, 
											const char *p, mtk_size_t l, mtk_slice_t) {
		mtk_queue_t q = ((mtk_queue_t)arg);
		mtk_login_cid_t lcid = mtk_svconn_defer_login(c);
		//receiver send reply by using mtk_svconn_finish_login
		mtk_svevent_t *ev = (mtk_svevent_t *)std::malloc(sizeof(mtk_svevent_t) + l);
		ev->lcid = lcid;
		ev->cid = cid;
		ev->msgid = msgid;
		ev->datalen = l;
		std::memcpy(ev->data, p, l);
		mtk_queue_push(q, ev);
		return 0;
	}
	static void _DestroyEvent(void *p) {
		std::free(p);
	}
};

/* credential generation */
static bool CreateCred(const mtk_addr_t &settings, RPCStream::CredOptions &options) {
    if (settings.cert == nullptr) {
        return false;
    }
    options.pem_cert_chain = settings.cert;
    options.pem_private_key = settings.key;
    options.pem_root_certs = settings.ca;
    return true;
}

/* mtk specific server runner */
class Server : public IServer {
public:
	Server(const mtk_addr_t *addrs, int n_addrs, const mtk_svconf_t &conf) : 
		IServer(), n_addrs_(n_addrs), queue_(nullptr), conf_(conf) {
		addrs_ = new mtk_addr_t[n_addrs];
		for (int i = 0; i < n_addrs_; i++) { addrs_[i] = addrs[i]; }
	}
	~Server() {
		if (queue_ != nullptr) {
			mtk_queue_destroy(queue_);
		}
		if (addrs_ != nullptr) {
			delete []addrs_;
		}
	}
	inline mtk_queue_t Queue() { return queue_; }
	void Run() override {
	    CredOptions opts;
	    if (conf_.use_queue) {
	        auto r = std::unique_ptr<QueueReadHandler>(new QueueReadHandler());
	        queue_ = r->Queue();
	        Kick(addrs_, n_addrs_, conf_.n_worker, r.get());
	    } else {
	        auto r = std::unique_ptr<FunctionHandler>(new FunctionHandler(conf_.handler, conf_.acceptor, conf_.closer));
	        Kick(addrs_, n_addrs_, conf_.n_worker, r.get());
	    }
	}
private:
	mtk_addr_t *addrs_;
	int n_addrs_;
	mtk_queue_t queue_;
	mtk_svconf_t conf_;
};

/* closure wrapper */
class Closure : public mtk_closure_t {
public:
	void operator () (mtk_result_t r, const char *p, mtk_size_t l) {
		on_msg(arg, r, p, l);
	}
	void operator () (mtk_cid_t cid, const char *p, mtk_size_t l) {
		on_connect(arg, cid, p, l);
	}
};

/* client */
class Client : public RPCStream, RPCStream::IClientDelegate {
protected:
	uint64_t cid_;
	mtk_clconf_t clconf_;
	SystemPayload::Connect crep_;
public:
	Client(mtk_clconf_t *clconf) : RPCStream(this), cid_(0), clconf_(*clconf) {}
	//implements RPCStream::IClientDelegate
	uint64_t Id() const override { return cid_; }
   	bool Ready() const override { 
   		return mtk_closure_valid(&clconf_.on_ready) ? 
   			mtk_closure_call_noarg(&clconf_.on_ready, on_ready) : 
   			true; 
   	}
    bool AddPayload(SystemPayload::Connect &c) override {
    	MemSlice s;
    	uint64_t cid = mtk_closure_call(&clconf_.on_start, on_start, &s);
    	c.set_id(cid);
    	if (s.len_ > 0) {
	    	c.set_payload(s.ptr_, s.len_);
	    }
    	return true;
    }
    bool OnOpenStream(mtk_result_t r, const char *p, mtk_size_t len) override {
    	if (r < 0) {
	    	return mtk_closure_call(&(clconf_.on_connect), on_connect, 0, "", 0);
    	}
    	if (Codec::Unpack((const uint8_t *)p, len, crep_) < 0) {
			return false;
		}
		cid_ = crep_.id();
		return mtk_closure_call(&clconf_.on_connect, on_connect, cid_, crep_.payload().c_str(), crep_.payload().length());;
    }
    mtk_time_t OnCloseStream(int reconnect_attempt) override {
    	mtk_time_t dur = 0;
    	if (mtk_closure_valid(&clconf_.on_close)) {
    		dur = mtk_closure_call(&clconf_.on_close, on_close, cid_, reconnect_attempt);
    	}
    	return dur != 0 ? dur : CalcReconnectWaitDuration(reconnect_attempt);
    }
    void Poll() override {}
};

/* Queue */
class Queue : public moodycamel::ConcurrentQueue<void *> {
protected:
	mtk_queue_elem_free_t dtor_;
public:
	Queue(mtk_queue_elem_free_t dtor) : moodycamel::ConcurrentQueue<void *>() {
		dtor_ = dtor;
	}
	~Queue() {
		void *ptr;
		while (try_dequeue(ptr)) {
			FreeElement(ptr);
		}
	}
	inline void FreeElement(void *elem) {
		if (dtor_ != nullptr) { dtor_(elem); }
	}
};


/******* grpc client/server API *******/
void mtk_listen(mtk_addr_t *addr, int n_addr, mtk_svconf_t *svconf, mtk_server_t *psv) {
	Server *sv = new Server(addr, n_addr, *svconf);
	*psv = sv;
	if (svconf->exclusive) {
		sv->Run();
	} else {
		sv->Start();
	}
}
void mtk_server_shutdown(mtk_server_t sv) {
	((Server *)sv)->Shutdown();
}
void mtk_server_join(mtk_server_t sv) {
	((Server *)sv)->Join();
}
mtk_queue_t mtk_server_queue(mtk_server_t sv) {
	return ((Server *)sv)->Queue();
}
mtk_login_cid_t mtk_svconn_defer_login(mtk_svconn_t conn) {
	return Conn::DeferLogin(conn);
}
mtk_svconn_t mtk_svconn_find_deferred(mtk_login_cid_t lcid) {
	return Conn::FindDeferred(lcid);
}
void mtk_svconn_finish_login(mtk_login_cid_t login_cid, mtk_cid_t cid, mtk_msgid_t msgid, const char *data, mtk_size_t datalen) {
	Conn::FinishLogin(login_cid, cid, msgid, data, datalen);
}
mtk_cid_t mtk_svconn_cid(mtk_svconn_t conn) {
	return ((Conn *)conn)->Id();
}
mtk_msgid_t mtk_svconn_msgid(mtk_svconn_t conn) {
	return ((Conn *)conn)->CurrentMsgId();
}
void mtk_svconn_send(mtk_svconn_t conn, mtk_msgid_t msgid, const char *data, mtk_size_t datalen) {
	std::string buf(data, datalen);
	((Conn *)conn)->Rep(msgid, buf);
}
void mtk_svconn_notify(mtk_svconn_t conn, uint32_t type, const char *data, mtk_size_t datalen) {
	std::string buf(data, datalen);
	((Conn *)conn)->Notify(type, buf);
}
void mtk_svconn_error(mtk_svconn_t conn, mtk_msgid_t msgid, const char *data, mtk_size_t datalen) {
	Error *e = new Error();
	e->set_error_code(MTK_APPLICATION_ERROR);
	e->set_payload(data, datalen);
	((Conn *)conn)->Throw(msgid, e);
}
void mtk_svconn_task(mtk_svconn_t conn, uint32_t type, const char *data, mtk_size_t datalen) {
	std::string buf(data, datalen);
	((Conn *)conn)->AddTask(type, buf);
}
void mtk_svconn_close(mtk_svconn_t conn) {
	((Conn *)conn)->InternalClose();
}
void mtk_svconn_putctx(mtk_svconn_t conn, void *ctx, mtk_ctx_free_t dtor) {
	Conn *c = (Conn *)conn;
	//TRACE("id = {} ctx = {} put into {}", c->Id(), ctx, (void *)c->AttachedWorker()->Server());
	c->SetUserCtx(ctx, dtor);
}
void mtk_svconn_sweep_ctx() {
	SVStream::SweepUserCtx();
}
void *mtk_svconn_getctx(mtk_svconn_t conn) {
	return ((Conn *)conn)->UserCtxPtr();
}
mtk_conn_t mtk_connect(mtk_addr_t *addr, mtk_clconf_t *clconf) {
	Client *cl = new Client(clconf);
	Client::CredOptions opts;
	cl->Initialize(addr->host, CreateCred(*addr, opts) ? &opts : nullptr);
	return (void *)cl;
}
void mtk_cid_send(mtk_server_t sv, mtk_cid_t cid, mtk_msgid_t msgid, const char *data, mtk_size_t datalen) {
	Conn::Stream s = Conn::Get((IServer *)sv, cid);
	if (s == nullptr) { return; }
	std::string buf(data, datalen);
	s->Rep(msgid, buf);
}
void mtk_cid_notify(mtk_server_t sv, mtk_cid_t cid, uint32_t type, const char *data, mtk_size_t datalen) {
	Conn::Stream s = Conn::Get((IServer *)sv, cid);
	if (s == nullptr) { return; }
	std::string buf(data, datalen);
	s->Notify(type, buf);
}
void mtk_cid_error(mtk_server_t sv, mtk_cid_t cid, mtk_msgid_t msgid, const char *data, mtk_size_t datalen) {
	Conn::Stream s = Conn::Get((IServer *)sv, cid);
	if (s == nullptr) { return; }
	Error *e = new Error();
	e->set_error_code(MTK_APPLICATION_ERROR);
	e->set_payload(data, datalen);
	s->Throw(msgid, e);
}
void mtk_cid_task(mtk_server_t sv, mtk_cid_t cid, uint32_t type, const char *data, mtk_size_t datalen) {
	Conn::Stream s = Conn::Get((IServer *)sv, cid);
	if (s == nullptr) { return; }
	std::string buf(data, datalen);
	s->AddTask(type, buf);
}
void mtk_cid_close(mtk_server_t sv, mtk_cid_t cid) {
	Conn::Stream s = Conn::Get((IServer *)sv, cid);
	if (s == nullptr) { return; }
	s->Close();
}
void *mtk_cid_getctx(mtk_server_t sv, mtk_cid_t cid) {
	Conn::Stream s = Conn::Get((IServer *)sv, cid);
	if (s == nullptr) { 
		TRACE("id = {} cannot found in {}", cid, (void *)sv);
		return nullptr; 
	}
	return s->UserCtxPtr();
}
mtk_cid_t mtk_conn_cid(mtk_conn_t c) {
	Client *cl = (Client *)c;
	return cl->Id();	
}
void mtk_conn_poll(mtk_conn_t c) {
	Client *cl = (Client *)c;
	cl->Update();
}
void mtk_conn_close(mtk_conn_t c) {
	Client *cl = (Client *)c;
	cl->Finalize();
	delete cl;
}
void mtk_conn_reset(mtk_conn_t c) {
	Client *cl = (Client *)c;
	cl->Release();
}
void mtk_conn_send(mtk_conn_t c, uint32_t type, const char *p, mtk_size_t plen, mtk_closure_t clsr) {
	Client *cl = (Client *)c;
	cl->Call(type, p, plen, *(Closure*)&clsr);
}
void mtk_conn_timeout(mtk_conn_t c, mtk_time_t duration) {
	Client *cl = (Client *)c;
	cl->SetDefaultTimeout(duration);
}
mtk_time_t mtk_conn_reconnect_wait(mtk_conn_t c) {
	Client *cl = (Client *)c;
	return cl->ReconnectWait();
}
void mtk_conn_watch(mtk_conn_t c, mtk_closure_t clsr) {
	Client *cl = (Client *)c;
	cl->RegisterNotifyCB(*(Closure*)&clsr);
}
bool mtk_conn_connected(mtk_conn_t c) {
	Client *cl = (Client *)c;
	return cl->IsConnected();
}



/******* http API *******/
std::thread s_websv_thread;
int s_num_websv_port = 0;

bool mtk_httpsrv_listen(int port, mtk_closure_t cb) {
	if (s_num_websv_port <= 0) {
		if (!HttpServer::Instance().Init()) { return false; }
	}
	if (!HttpServer::Instance().Listen(port, [cb](HttpFSM &req, HttpServer::IResponseWriter &rep) {
		mtk_closure_call(&cb, on_httpsrv, (void *)&req, (void *)&rep);
	})) { return false; }
	s_num_websv_port++;
	return true;
}
bool mtk_http_start(const char *root_cert) {
	extern std::string ssl_client_root_cert;
	//TODO: unify HttpClient and Server thread
	if (!HttpClient::Start(root_cert != nullptr ? root_cert : ssl_client_root_cert.c_str())) {
		return false;
 	}
	if (s_num_websv_port > 0) {
		s_websv_thread = std::thread([] {
			HttpServer::Instance().Run();
		});
	}
	return true;
}
void mtk_http_stop() {
	HttpClient::Stop();
	if (s_websv_thread.joinable()) {
		HttpServer::Instance().Fin();
		s_websv_thread.join();
	}
}
bool mtk_http_enabled() {
	return HttpClient::Available();
}
static void mtk_httpcli_get_raw(bool secure, const char *host, const char *path,
                        mtk_http_header_t *headers, int n_headers,
                        mtk_closure_t cb) {
	HttpClient::Get(host, path, (grpc_http_header *)headers, n_headers, 
	[cb](int st, grpc_http_header *h, mtk_size_t hl, const char *r, mtk_size_t rlen) {
		mtk_closure_call(&cb, on_httpcli, st, (mtk_http_header_t *)h, hl, r, rlen);
	}, secure);
}
static void mtk_httpcli_post_raw(bool secure, const char *host, const char *path,
                        mtk_http_header_t *headers, int n_headers,
						const char *body, int blen, 
                        mtk_closure_t cb) {
	HttpClient::Post(host, path, (grpc_http_header *)headers, n_headers, body, blen, 
	[cb](int st, grpc_http_header *h, mtk_size_t hl, const char *r, mtk_size_t rlen) {
		mtk_closure_call(&cb, on_httpcli, st, (mtk_http_header_t *)h, hl, r, rlen);
	}, secure);
}
void mtk_httpcli_get(const char *host, const char *path,
                        mtk_http_header_t *headers, int n_headers,
                        mtk_closure_t cb) {
	mtk_httpcli_get_raw(true, host, path, headers, n_headers, cb);
}
void mtk_httpcli_post(const char *host, const char *path,
                        mtk_http_header_t *headers, int n_headers,
						const char *body, int blen, 
                        mtk_closure_t cb) {
	mtk_httpcli_post_raw(true, host, path, headers, n_headers, body, blen, cb);
}
void mtk_httpcli_get_insecure(const char *host, const char *path,
                        mtk_http_header_t *headers, int n_headers,
                        mtk_closure_t cb) {
	mtk_httpcli_get_raw(false, host, path, headers, n_headers, cb);
}
void mtk_httpcli_post_insecure(const char *host, const char *path,
                        mtk_http_header_t *headers, int n_headers,
						const char *body, int blen, 
                        mtk_closure_t cb) {
	mtk_httpcli_post_raw(false, host, path, headers, n_headers, body, blen, cb);
}
const char *mtk_httpsrv_read_path(mtk_httpsrv_request_t req, char *value, mtk_size_t *size) {
	HttpFSM *fsm = (HttpFSM *)req;
	size_t tmp;
	const char *ret = fsm->url(value, *size, &tmp);
	if (ret != nullptr) {
		*size = tmp;
	}
	return ret;
}
const char *mtk_httpsrv_read_header(mtk_httpsrv_request_t req, const char *key, char *value, mtk_size_t *size) {
	HttpFSM *fsm = (HttpFSM *)req;
	int inlen = *size, outlen;
	if (fsm->hdrstr(key, value, inlen, &outlen)) {
		*size = outlen;
		return value;
	}
	return nullptr;
}
const char *mtk_httpsrv_read_body(mtk_httpsrv_request_t req, mtk_size_t *size) {
	HttpFSM *fsm = (HttpFSM *)req;
	*size = fsm->bodylen();
	return fsm->body();	
}
void mtk_httpsrv_write_header(mtk_httpsrv_response_t res, int status, mtk_http_header_t *hcl, mtk_size_t n_hcl) {
	HttpServer::IResponseWriter *writer = (HttpServer::IResponseWriter *)res;
	writer->WriteHeader((http_result_code_t)status, (grpc_http_header *)hcl, n_hcl);
}
void mtk_httpsrv_write_body(mtk_httpsrv_response_t res, const char *buffer, mtk_size_t len) {
	HttpServer::IResponseWriter *writer = (HttpServer::IResponseWriter *)res;
	writer->WriteBody((const uint8_t *)buffer, len);
}



/******* util API *******/
mtk_time_t mtk_time() {
	return clock::now();
}
mtk_second_t mtk_second() {
	long t, nt;
	clock::now(t, nt);
	return (mtk_second_t)t;
}
mtk_time_t mtk_sleep(mtk_time_t d) {
	return clock::sleep(d);
}
mtk_time_t mtk_pause(mtk_time_t d) {
	return clock::pause(d);
}

void mtk_log_config(const char *svname, mtk_logger_cb_t cb) {
	logger::configure(cb, svname);
}
void mtk_log(mtk_loglevel_t lv, const char *fmt, size_t len, ...) {
	char buffer[len + 1];
	va_list args;
	va_start(args, len);
	vsnprintf(buffer, len + 1, fmt, args);
	va_end(args);
	logger::log_no_arg((logger::level::def)lv, buffer);
}
void mtk_log_flush() {
	logger::flush_from_main_thread();
}


mtk_closure_t mtk_closure_nop = { nullptr, { nullptr } };

mtk_queue_t mtk_queue_create(mtk_queue_elem_free_t dtor) {
	return new Queue(dtor);
}
void mtk_queue_destroy(mtk_queue_t q) {
	delete (Queue *)q;
}
void mtk_queue_push(mtk_queue_t q, void *elem) {
	((Queue *)q)->enqueue(elem);
}
bool mtk_queue_pop(mtk_queue_t q, void **elem) {
	return ((Queue *)q)->try_dequeue(*elem);
}
void mtk_queue_elem_free(mtk_queue_t q, void *elem) {
	((Queue *)q)->FreeElement(elem);
}

/* slice */
mtk_slice_t mtk_slice_create() {
	return (mtk_slice_t)new MemSlice();
}
void mtk_slice_put(mtk_slice_t s, const char *p, mtk_size_t l) {
	((MemSlice *)s)->Put(p, l);
}
void mtk_slice_destroy(mtk_slice_t s) {
	delete (MemSlice *)s;
}

/* lib ref/unref */
void mtk_lib_ref() {
	PinLibrary();
}
void mtk_lib_unref() {
	UnpinLibrary();
}
