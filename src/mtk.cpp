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
		g_svthread = std::move(std::thread([svconf, has_cred, &opts] {
			auto r = std::unique_ptr<ReadHandler>(new ReadHandler(svconf->handler));
			auto w = std::unique_ptr<WriteHandler>(new WriteHandler(svconf->handler));
			ServerRunner::Instance().Run(*svconf, r.get(), w.get(), has_cred ? &opts : nullptr);
	    }));
	}
}
mtk_svconn_t mtk_svconn_find(uint64_t id) {
	return nullptr;
}
void mtk_svconn_id(mtk_svconn_t conn) {

}
void mtk_svconn_send(mtk_svconn_t conn, const char *data, size_t datalen) {

}
void mtk_svconn_add_task(mtk_svconn_t conn, const char *data, size_t datalen) {

}
void mtk_svconn_close(mtk_svconn_t conn) {

}

mtk_conn_t mtk_connect(mtk_addr_t *addr, mtk_clconf_t *clconf) {
	DuplexStream *ds = new StreamDelegate(clconf);
	DuplexStream::CredOptions opts;
	bool has_cred = DuplexStream::CreateCred(*addr, opts);
	ds->Initialize(addr->host, has_cred ? &opts : nullptr);
	return (void *)ds;
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
