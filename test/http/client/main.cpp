#include <mtk.h>
#include <cstdlib>
#include <string.h>

static void recvresp(void *p, int st, mtk_http_header_t*, mtk_size_t, const char*, mtk_size_t) {
	MTK_LOG(info, "ev:http response,st:%d", st);
	*((int *)p) = ((st == 200) ? 1 : -1);
}

int main(int argc, char *argv[]) {
	mtk_closure_t clsr;
	int res = 0;
	mtk_closure_init(&clsr, on_httpcli, recvresp, &res);
	mtk_http_start(nullptr);
	const char *endpoint = argc > 1 ? argv[1] : "http://localhost:8008";
	bool ssl; const char *host;
	if (strncmp(endpoint, "http://", 7) == 0) {
		ssl = false;
		host = endpoint + 7;
	} else if (strncmp(endpoint, "https://", 8) == 0) {
		ssl = true;
		host = endpoint + 8;
	} else {
		ssl = false;
		host = endpoint;
	}
	(ssl ? mtk_httpcli_get : mtk_httpcli_get_insecure)(
		host, argc > 2 ? argv[2] : "/", nullptr, 0, clsr
	);
	while (res == 0) {
		mtk_sleep(mtk_msec(50));
	}
	mtk_http_stop();
	MTK_TRACE("ev:exit,ec:%d", res);
	if (res < 0) {
		std::exit(res);
	}
	return 0;
}
