#include "mtk.h"
#include "stream.h"
#include <thread>

using namespace mtk;

std::thread g_svthread;

mtk_server_t mtk_listen(mtk_addr_t *addr, mtk_svconf_t *svconf) {
	CredOptions opts;
	bool has_cred = DuplexStream::CreateCred(*addr, opts);
	svconf->listen_at = addr->host;
	if (svconf->exclusive) {
		ServerRunner::Instance().Run(*svconf, has_cred ? &opts : nullptr);
	} else {
		g_svthread = std::move(std::thread([svconf, has_cred, &opts] {
			ServerRunner::Instance().Run(*svconf, has_cred ? &opts : nullptr);
	    }));
	}
}
mtk_conn_t mtk_connect(mtk_addr_t *addr, mtk_clconf_t *clconf) {
	StreamDelegate *d = new StreamDelegate(clconf);
	DuplexStream *ds = new DuplexStream(d);
	d->SetStream(ds);
	CredOptions opts;
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
