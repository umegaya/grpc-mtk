#include "mtk.h"
#include "stream.h"
#include "conn.h"
#include "server.h"
#include "delegate.h"
#include "http.h"
#include <grpc++/grpc++.h>
#include <thread>

using namespace mtk;

/******* internal bridge *******/
template <class S>
class FunctionHandler : public IHandler {
protected:
	mtk_server_callback_t handler_;
public:
	FunctionHandler(mtk_server_callback_t handler) : handler_(handler) {}
	//implements IHandler
	grpc::Status Handle(IConn *c, Request &req) {
		return handler_(c, req.type(), req.payload().c_str(), req.payload().length()) >= 0 ? grpc::Status::OK : grpc::Status::CANCELLED;
	}
	IConn *NewConn(IWorker *worker, Service *service, IHandler *handler, ServerCompletionQueue *cq) {
		return new Conn<S>(worker, service, handler, cq);
	}
};
typedef FunctionHandler<RSVStream> ReadHandler;
typedef FunctionHandler<WSVStream> WriteHandler;
typedef Conn<RSVStream> RConn;



/******* grpc client/server API *******/
std::thread g_svthread;

void mtk_listen(mtk_addr_t *addr, mtk_svconf_t *svconf) {
	DuplexStream::ServerCredOptions opts;
	bool has_cred = DuplexStream::CreateCred(*addr, opts);
	svconf->listen_at = addr->host;
	if (svconf->exclusive) {
		auto r = std::unique_ptr<ReadHandler>(new ReadHandler(svconf->handler));
		auto w = std::unique_ptr<WriteHandler>(new WriteHandler(svconf->handler));
		ServerRunner::Instance().Run(*svconf, r.get(), w.get(), has_cred ? &opts : nullptr);
	} else {
		g_svthread = std::thread([svconf, has_cred, &opts] {
			auto r = std::unique_ptr<ReadHandler>(new ReadHandler(svconf->handler));
			auto w = std::unique_ptr<WriteHandler>(new WriteHandler(svconf->handler));
			ServerRunner::Instance().Run(*svconf, r.get(), w.get(), has_cred ? &opts : nullptr);
	    });
	    g_svthread.join();
	}
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
void mtk_svconn_task(mtk_svconn_t conn, uint32_t type, const char *data, size_t datalen) {
	std::string buf(data, datalen);
	((RConn *)conn)->AddTask(type, buf);
}
void mtk_svconn_close(mtk_svconn_t conn) {
	((RConn *)conn)->InternalClose();
}
void mtk_cid_send(mtk_cid_t cid, mtk_msgid_t msgid, const char *data, size_t datalen) {
	RConn::Stream s = RConn::Get(cid);
	return mtk_svconn_send(s.get(), msgid, data, datalen);
}
void mtk_cid_notify(mtk_cid_t cid, uint32_t type, const char *data, size_t datalen) {
	RConn::Stream s = RConn::Get(cid);
	return mtk_svconn_notify(s.get(), type, data, datalen);

}
void mtk_cid_task(mtk_cid_t cid, uint32_t type, const char *data, size_t datalen) {
	RConn::Stream s = RConn::Get(cid);
	return mtk_svconn_task(s.get(), type, data, datalen);

}
void mtk_cid_close(mtk_cid_t cid) {
	RConn::Stream s = RConn::Get(cid);
	return mtk_svconn_close(s.get());
}



mtk_conn_t mtk_connect(mtk_addr_t *addr, mtk_clconf_t *clconf) {
	DuplexStream *ds = new StreamDelegate(clconf);
	DuplexStream::CredOptions opts;
	bool has_cred = DuplexStream::CreateCred(*addr, opts);
	ds->Initialize(addr->host, has_cred ? &opts : nullptr);
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
void mtk_conn_send(mtk_conn_t c, uint32_t type, const char *p, size_t plen, mtk_callback_t cb) {
	DuplexStream *ds = (DuplexStream *)c;
	ds->Call(type, p, plen, cb);
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
void mtk_http_server_write_body(mtk_http_server_response_t *res, char *buffer, size_t len) {
	HttpServer::IResponseWriter *writer = (HttpServer::IResponseWriter *)res;
	writer->WriteBody((const uint8_t *)buffer, len);
}



/******* util API *******/
mtk_time_t mtk_time() {
	return 0;
}
void mtk_sleep(mtk_time_t d) {

}

