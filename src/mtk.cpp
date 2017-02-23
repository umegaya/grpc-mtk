#include "mtk.h"
#include "stream.h"
#include "worker.h"
#include "conn.h"
#include "server.h"
#include "codec.h"
#include "http.h"
#include "timespec.h"
#include <grpc++/grpc++.h>
#include <thread>

using namespace mtk;

/******* internal bridge *******/
/* worker handler */
template <class S>
class FunctionHandler : public IHandler {
protected:
	mtk_server_recv_cb_t handler_;
	mtk_server_accept_cb_t acceptor_;
public:
	FunctionHandler(mtk_server_recv_cb_t handler, mtk_server_accept_cb_t acceptor) : 
		handler_(handler), acceptor_(acceptor) {}
	//implements IHandler
	grpc::Status Handle(IConn *c, Request &req) {
		return handler_(c, req.type(), req.payload().c_str(), req.payload().length()) >= 0 ? 
			grpc::Status::OK : grpc::Status::CANCELLED;
	}
	mtk_cid_t Accept(IConn *c, Request &req) {
		SystemPayload::Connect creq;
		if (Codec::Unpack((const uint8_t *)req.payload().c_str(), req.payload().length(), creq) < 0) {
			return 0;
		}
		char *rep; size_t rlen = 0;
		mtk_cid_t cid = acceptor_(c, creq.id(), creq.payload().c_str(), creq.payload().length(), &rep, &rlen);
		if (cid != 0) {
			SystemPayload::Connect sysrep;
			sysrep.set_id(cid);
			if (rlen > 0) {
				sysrep.set_payload(rep, rlen);
				free(rep);
			}
			((Conn<S> *)c)->SysRep(req.msgid(), sysrep);
		} else {
			Error *e = new Error();
			e->set_error_code(MTK_ACCEPT_DENY);
			((Conn<S> *)c)->Throw(req.msgid(), e);
		}
		return cid;
	}
	IConn *NewConn(IWorker *worker, Service *service, IHandler *handler, ServerCompletionQueue *cq) {
		return new RConn(worker, service, handler, cq);
	}
};
typedef FunctionHandler<RSVStream> ReadHandler;
class WriteHandler : public FunctionHandler<WSVStream> {
public:
	WriteHandler(mtk_server_recv_cb_t handler, mtk_server_accept_cb_t acceptor) : 
		FunctionHandler<WSVStream>(handler, acceptor) {}
	mtk_cid_t Accept(IConn *c, Request &req) {
		mtk_cid_t cid = FunctionHandler<WSVStream>::Accept(c, req);
		if (cid != 0) {
			if (!RConn::MakePair(cid, *(WConn *)c)) {
				TRACE("RConn::MakePair: fails");
			}
		}
		return cid;	
	}	
	IConn *NewConn(IWorker *worker, Service *service, IHandler *handler, ServerCompletionQueue *cq) {
		return new WConn(worker, service, handler, cq);
	}
};

/* credentical generation */
bool CreateCred(mtk_addr_t &settings, DuplexStream::CredOptions &options) {
    if (settings.cert == nullptr) {
        return false;
    }
    options.pem_cert_chain = settings.cert;
    options.pem_private_key = settings.key;
    options.pem_root_certs = settings.ca;
    return true;
}
bool CreateCred(mtk_addr_t &settings, DuplexStream::ServerCredOptions &options) {
    if (settings.cert == nullptr) {
        return false;
    }
    options.pem_root_certs = settings.ca;
    options.pem_key_cert_pairs = {
        { .private_key = settings.key, .cert_chain = settings.cert },
    };
    return true;
}

/* closure wrapper */
class Closure : public mtk_closure_t {
public:
	void operator () (mtk_result_t r, const char *p, size_t l) {
		on_msg(arg, r, p, l);
	}
	void operator () (mtk_cid_t cid, const char *p, size_t l) {
		on_connect(arg, cid, p, l);
	}
};

/* client stream delegate */
class StreamDelegate : public DuplexStream, IDuplexStreamDelegate {
protected:
	mtk_clconf_t clconf_;
	SystemPayload::Connect crep_;
public:
	StreamDelegate(mtk_clconf_t *clconf) : DuplexStream(this), clconf_(*clconf) {}
	uint64_t Id() const { return clconf_.id; }
   	bool Valid() const { return clconf_.validate == nullptr ? true : clconf_.validate(); }
    bool AddPayload(SystemPayload::Connect &c, int stream_idx) {
    	c.set_id(clconf_.id);
    	c.set_payload(clconf_.payload, clconf_.payload_len);
    	return true;
    }
    bool OnOpenStream(mtk_result_t r, const char *p, size_t len, int stream_idx) {
    	if (r < 0) {
	    	return mtk_closure_call(&(clconf_.on_connect), on_connect, 0, "", 0);
    	}
    	if (stream_idx == WRITE) {
    		if (Codec::Unpack((const uint8_t *)p, len, crep_) < 0) {
    			return false;
    		}
			clconf_.id = crep_.id();
			return true;
		} else if (stream_idx == READ) {
	    	return mtk_closure_call(&(clconf_.on_connect), on_connect, crep_.id(), crep_.payload().c_str(), crep_.payload().length());
		} else {
			ASSERT(false);
			return false;
		}
    }
    void Poll() {}
};



/******* grpc client/server API *******/
std::thread g_svthread;

void mtk_listen(mtk_addr_t *addr, mtk_svconf_t *svconf) {
	DuplexStream::ServerCredOptions opts;
	bool has_cred = CreateCred(*addr, opts);
	svconf->listen_at = addr->host;
	if (svconf->exclusive) {
		auto r = std::unique_ptr<ReadHandler>(new ReadHandler(svconf->handler, svconf->acceptor));
		auto w = std::unique_ptr<WriteHandler>(new WriteHandler(svconf->handler, svconf->acceptor));
		ServerRunner::Instance().Run(*svconf, r.get(), w.get(), has_cred ? &opts : nullptr);
	} else {
		g_svthread = std::thread([svconf, has_cred, &opts] {
			auto r = std::unique_ptr<ReadHandler>(new ReadHandler(svconf->handler, svconf->acceptor));
			auto w = std::unique_ptr<WriteHandler>(new WriteHandler(svconf->handler, svconf->acceptor));
			ServerRunner::Instance().Run(*svconf, r.get(), w.get(), has_cred ? &opts : nullptr);
	    });
	}
}
void mtk_join_server() {
	if (g_svthread.joinable()) {
		g_svthread.join();
	}
}
void mtk_svconn_accept(mtk_svconn_t conn, mtk_cid_t cid) {
	((RConn *)conn)->Register(cid);
}
mtk_cid_t mtk_svconn_cid(mtk_svconn_t conn) {
	return ((RConn *)conn)->Id();
}
mtk_msgid_t mtk_svconn_msgid(mtk_svconn_t conn) {
	return ((RConn *)conn)->CurrentMsgId();
}
void mtk_svconn_send(mtk_svconn_t conn, mtk_msgid_t msgid, const char *data, size_t datalen) {
	std::string buf(data, datalen);
	((RConn *)conn)->Rep(msgid, buf);
}
void mtk_svconn_notify(mtk_svconn_t conn, uint32_t type, const char *data, size_t datalen) {
	std::string buf(data, datalen);
	((RConn *)conn)->Notify(type, buf);
}
void mtk_svconn_raise(mtk_svconn_t conn, mtk_msgid_t msgid, mtk_result_t errcode, const char *data, size_t datalen) {
	Error *e = new Error();
	e->set_error_code(errcode);
	e->set_payload(data, datalen);
	((RConn *)conn)->Throw(msgid, e);
}
void mtk_svconn_task(mtk_svconn_t conn, uint32_t type, const char *data, size_t datalen) {
	std::string buf(data, datalen);
	((RConn *)conn)->AddTask(type, buf);
}
void mtk_svconn_close(mtk_svconn_t conn) {
	((RConn *)conn)->InternalClose();
}
void mtk_cid_send(mtk_cid_t cid, mtk_msgid_t msgid, const char *data, size_t datalen) {
	RConn::Stream s = RConn::Get(cid);
	if (s == nullptr) { return; }
	return mtk_svconn_send(s.get(), msgid, data, datalen);
}
void mtk_cid_notify(mtk_cid_t cid, uint32_t type, const char *data, size_t datalen) {
	RConn::Stream s = RConn::Get(cid);
	if (s == nullptr) { return; }
	return mtk_svconn_notify(s.get(), type, data, datalen);
}
void mtk_cid_raise(mtk_cid_t cid, mtk_msgid_t msgid, mtk_result_t errcode, const char *data, size_t datalen) {
	RConn::Stream s = RConn::Get(cid);
	if (s == nullptr) { return; }
	return mtk_svconn_raise(s.get(), msgid, errcode, data, datalen);
}
void mtk_cid_task(mtk_cid_t cid, uint32_t type, const char *data, size_t datalen) {
	RConn::Stream s = RConn::Get(cid);
	if (s == nullptr) { return; }
	return mtk_svconn_task(s.get(), type, data, datalen);

}
void mtk_cid_close(mtk_cid_t cid) {
	RConn::Stream s = RConn::Get(cid);
	if (s == nullptr) { return; }
	return mtk_svconn_close(s.get());
}



mtk_conn_t mtk_connect(mtk_addr_t *addr, mtk_clconf_t *clconf) {
	DuplexStream *ds = new StreamDelegate(clconf);
	DuplexStream::CredOptions opts;
	ds->Initialize(addr->host, CreateCred(*addr, opts) ? &opts : nullptr);
	return (void *)ds;
}
mtk_cid_t mtk_conn_cid(mtk_conn_t c) {
	DuplexStream *ds = (DuplexStream *)c;
	return ds->Id();
}
void mtk_conn_poll(mtk_conn_t c) {
	DuplexStream *ds = (DuplexStream *)c;
	ds->Update();
}
void mtk_conn_close(mtk_conn_t c) {
	DuplexStream *ds = (DuplexStream *)c;
	ds->Release();
}
void mtk_conn_send(mtk_conn_t c, uint32_t type, const char *p, size_t plen, mtk_closure_t clsr) {
	DuplexStream *ds = (DuplexStream *)c;
	ds->Call(type, p, plen, *(Closure*)&clsr);
}
void mtk_conn_watch(mtk_conn_t c, uint32_t type, mtk_closure_t clsr) {
	DuplexStream *ds = (DuplexStream *)c;
	ds->RegisterNotifyCB(type, *(Closure*)&clsr);
}



/******* http API *******/
std::thread s_websv_thread;
int s_num_websv_port = 0;

void mtk_http_start(const char *root_cert, bool start_server) {
	//TODO: unify HttpClient and Server thread
	HttpClient::Start(root_cert);
	if (s_num_websv_port > 0) {
		s_websv_thread = std::thread([] {
			HttpServer::Instance().Run();
		});
	}
}
bool mtk_http_listen(int port, mtk_http_server_cb_t cb) {
	if (s_num_websv_port <= 0) {
		if (!HttpServer::Instance().Init()) { return false; }
	}
	if (!HttpServer::Instance().Listen(port, [cb](HttpFSM &req, HttpServer::IResponseWriter &rep) {
		cb((void *)&req, (void *)&rep);
	})) { return false; }
	s_num_websv_port++;
	return true;
}
void mtk_http_stop() {
	HttpClient::Stop();
	if (s_websv_thread.joinable()) {
		s_websv_thread.join();
	}
}
void mtk_http_get(const char *host, const char *path,
                        mtk_http_header_t *headers, int n_headers,
                        mtk_http_client_cb_t cb) {
	HttpClient::Get(host, path, (grpc_http_header *)headers, n_headers, 
	[cb](int st, grpc_http_header *h, size_t hl, const char *r, size_t rlen) {
		cb(st, (mtk_http_header_t *)h, hl, r, rlen);
	});
}
void mtk_http_post(const char *host, const char *path,
                        mtk_http_header_t *headers, int n_headers,
						const char *body, int blen, 
                        mtk_http_client_cb_t cb) {
	HttpClient::Post(host, path, (grpc_http_header *)headers, n_headers, body, blen, 
	[cb](int st, grpc_http_header *h, size_t hl, const char *r, size_t rlen) {
		cb(st, (mtk_http_header_t *)h, hl, r, rlen);
	});
}
extern bool mtk_http_server_read_header(mtk_http_server_request_t *req, const char *key, char *value, size_t *size) {
	HttpFSM *fsm = (HttpFSM *)req;
	int inlen = *size, outlen;
	if (fsm->hdrstr(key, value, inlen, &outlen)) {
		*size = outlen;
		return true;
	}
	return false;
}
const char *mtk_http_server_read_body(mtk_http_server_request_t *req, size_t *size) {
	HttpFSM *fsm = (HttpFSM *)req;
	*size = fsm->bodylen();
	return fsm->body();	
}
void mtk_http_server_write_header(mtk_http_server_response_t *res, int status, mtk_http_header_t *hds, size_t n_hds) {
	HttpServer::IResponseWriter *writer = (HttpServer::IResponseWriter *)res;
	writer->WriteHeader((http_result_code_t)status, (grpc_http_header *)hds, n_hds);
}
void mtk_http_server_write_body(mtk_http_server_response_t *res, const char *buffer, size_t len) {
	HttpServer::IResponseWriter *writer = (HttpServer::IResponseWriter *)res;
	writer->WriteBody((const uint8_t *)buffer, len);
}



/******* util API *******/
mtk_time_t mtk_time() {
	return clock::now();
}
mtk_time_t mtk_sleep(mtk_time_t d) {
	return clock::sleep(d);
}
void mtk_log_init() {
	logger::Initialize();
}
