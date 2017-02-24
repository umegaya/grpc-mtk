#include <mtk.h>
#include <cstdlib>

static void recvresp(void *p, int st, mtk_http_header_t*, size_t, const char*, size_t) {
	*((int *)p) = ((st == 200) ? 1 : -1);
}

int main(int argc, char *argv[]) {
	mtk_closure_t clsr;
	int res = 0;
	mtk_closure_init(&clsr, on_httpcli, recvresp, &res);
	mtk_http_start(nullptr);
	mtk_httpcli_get(argv[0], argv[1], nullptr, 0, clsr);
	while (res == 0) {
		mtk_sleep(mtk_msec(50));
	}
	if (res < 0) {
		std::exit(res);
	}
	return 0;
}
