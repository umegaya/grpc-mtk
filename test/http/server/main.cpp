#include <mtk.h>
#include <string>

static void echo(void *, mtk_httpsrv_request_t req, mtk_httpsrv_response_t res) {
	char buff[256];
	size_t len, bl = sizeof(buff);
	const char *body = mtk_httpsrv_read_body(req, &len);
	mtk_httpsrv_read_path(req, buff, &bl);
	if (len > 0) {
		mtk_http_header_t hds[] = {
			{ .key=(char *)"Content-Length", .value=(char *)std::to_string(len + bl).c_str() }
		};
		mtk_httpsrv_write_header(res, 200, hds, 1);
		mtk_httpsrv_write_body(res, body, len);
		mtk_httpsrv_write_body(res, buff, bl);
	} else {
		mtk_httpsrv_write_header(res, 200, nullptr, 0);		
	}
}

int main(int argc, char *argv[]) {
	mtk_closure_t clsr;
	mtk_closure_init(&clsr, on_httpsrv, echo, nullptr);
	mtk_httpsrv_listen(8008, clsr);
	mtk_http_start(nullptr);
	mtk_pause(mtk_msec(argc > 1 ? std::stoi(argv[1]) : 1500));
	mtk_http_stop();
}
