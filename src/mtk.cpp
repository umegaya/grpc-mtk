#include "mtk.h"
#include "stream.h"
#include "server.h"
#include "delegate.h"
#include "http.h"
#include <grpc++/grpc++.h>
#include <thread>

using namespace mtk;

std::thread g_svthread;

void mtk_listen(mtk_addr_t *addr, mtk_svconf_t *svconf) {
	DuplexStream::ServerCredOptions opts;
	bool has_cred = DuplexStream::CreateCred(*addr, opts);
	svconf->listen_at = addr->host;
	if (svconf->exclusive) {
		ServerRunner::Instance().Run(*svconf, nullptr, has_cred ? &opts : nullptr);
	} else {
		g_svthread = std::move(std::thread([svconf, has_cred, &opts] {
			ServerRunner::Instance().Run(*svconf, nullptr, has_cred ? &opts : nullptr);
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

void mtk_start_http(const char *root_cert) {
	HttpClient::Start(root_cert);
}
void mtk_stop_http() {
	HttpClient::Stop();
}
void mtk_http_get(const char *host, const char *path,
                        mtk_http_header_t *headers, int n_headers,
                        mtk_http_cb_t cb) {
	HttpClient::Get(host, path, (grpc_http_header *)headers, n_headers, 
	[cb](int st, grpc_http_header *h, size_t hl, const char *r, size_t rlen) {
		cb(st, (mtk_http_header_t *)h, hl, r, rlen);
	});
}
void mtk_http_post(const char *host, const char *path,
                        mtk_http_header_t *headers, int n_headers,
						const char *body, int blen, 
                        mtk_http_cb_t cb) {
	HttpClient::Post(host, path, (grpc_http_header *)headers, n_headers, body, blen, 
	[cb](int st, grpc_http_header *h, size_t hl, const char *r, size_t rlen) {
		cb(st, (mtk_http_header_t *)h, hl, r, rlen);
	});
}
