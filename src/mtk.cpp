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
class FunctionHandler : IHandler {
protected:
	mtk_server_callback_t handler_;
public:
	FunctionHandler(mtk_server_callback_t handler) : handler_(handler) {}
	//implements IHandler
	grpc::Status Handle(IConn *c, Request &req) {
		return handler_(req.type(), req.payload(), req.payload().length()) >= 0 ? grpc::Status::OK : grpc::Status::CANCELLED;
	}
	IConn *NewReader(IWorker *worker, Service *service, IHandler *handler, ServerCompletionQueue *cq) {
		return new Conn<RSVStream>(worker, service, handler, cq);
	}
	IConn *NewWriter(IWorker *worker, Service *service, IHandler *handler, ServerCompletionQueue *cq) {
		return new Conn<WSVStream>(worker, service, handler, cq);
	}
};


/******* grpc client/server API *******/
std::thread g_svthread;

void mtk_listen(mtk_addr_t *addr, mtk_svconf_t *svconf) {
	DuplexStream::ServerCredOptions opts;
	bool has_cred = DuplexStream::CreateCred(*addr, opts);
	svconf->listen_at = addr->host;
	auto handler = new FunctionHandler(svconf.handler);
	if (svconf->exclusive) {
		ServerRunner::Instance().Run(*svconf, handler, has_cred ? &opts : nullptr);
	} else {
		g_svthread = std::move(std::thread([svconf, has_cred, &opts] {
			ServerRunner::Instance().Run(*svconf, handler, has_cred ? &opts : nullptr);
	    }));
	}
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
void mtk_close(mtk_conn_t c) {
	DuplexStream *ds = (DuplexStream *)c;
	ds->Release();
}
void mtk_send(mtk_conn_t c, uint32_t type, const char *p, size_t plen, mtk_callback_t cb) {
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
			HttpServer::Run();
		});
	}
}
bool mtk_http_listen(int port, mtk_http_server_cb_t cb) {
	if (s_num_websv_port <= 0) {
		if (!HttpServer::Instance().Init()) { return false; }
	}
	if (!HttpServer::Instance().Listen(port, [cb](HttpFSM &req, IResponseWriter &rep) {
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
	if (fsm->hdrstr(key, value, inlen, outlen)) {
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
	IResponseWriter *writer = (IResponseWriter *)req;
	writer->WriteHeader(status, (grpc_http_header *)hds, n_hds);
}
void mtk_http_server_write_body(mtk_http_server_response_t *res, char *buffer, size_t len) {
	IResponseWriter *writer = (IResponseWriter *)req;
	writer->WriteBody(buffer, len);
}
