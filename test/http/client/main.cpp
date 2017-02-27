#include <mtk.h>
#include <cstdlib>

static void recvresp(void *p, int st, mtk_http_header_t*, mtk_size_t, const char*, mtk_size_t) {
	*((int *)p) = ((st == 200) ? 1 : -1);
}

int main(int argc, char *argv[]) {
	mtk_closure_t clsr;
	int res = 0;
	mtk_closure_init(&clsr, on_httpcli, recvresp, &res);
	mtk_http_start(nullptr);
	mtk_httpcli_get_insecure(
		argc > 1 ? argv[1] : "localhost:8008", 
		argc > 2 ? argv[2] : "/", 
		nullptr, 0, clsr);
	while (res == 0) {
		mtk_sleep(mtk_msec(50));
	}
	mtk_http_stop();
	if (res < 0) {
		std::exit(res);
	}
	return 0;
}
