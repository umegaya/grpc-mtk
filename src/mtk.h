#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef void *mtk_conn_t;
typedef void *mtk_svconn_t;
typedef void *mtk_reply_t;
typedef int mtk_result_t;
typedef void *(*mtk_context_factory_t)();
typedef void (*mtk_callback_t)(mtk_result_t, const char *, size_t);
typedef mtk_result_t (*mtk_acceptor_t)(mtk_svconn_t, mtk_result_t, const char *, size_t);
typedef bool (*mtk_valid_checker_t)();
typedef struct {
	const char *host, *cert, *key, *ca;
} mtk_addr_t;
typedef struct {
	struct {
		uint32_t n_reader, n_writer;
	} thread;
	bool exclusive; //if true, caller thread of mtk_listen blocks
	mtk_acceptor_t acceptor;
	mtk_context_factory_t ctx_factory;
	const char *listen_at;
	const char *root_cert;
} mtk_svconf_t;
typedef struct {
	uint64_t connect_id;
	const char *connect_payload;
	size_t connect_payload_len;
	mtk_callback_t login_cb;
	mtk_valid_checker_t valid_cb;
} mtk_clconf_t;
typedef enum {
	MTK_APPLICATION_ERROR = -1,
	MTK_TIMEOUT = -2,
} mtk_error_t;

extern void mtk_listen(mtk_addr_t *listen_at, mtk_svconf_t *conf);
extern mtk_conn_t mtk_connect(mtk_addr_t *connect_to, mtk_clconf_t *conf);
extern void mtk_conn_poll(mtk_conn_t conn);
extern void mtk_close(mtk_conn_t conn);
extern void mtk_send(mtk_conn_t conn, uint32_t type, const char *data, size_t datalen, mtk_callback_t cb);


typedef struct {
	char *key;
	char *value;
} mtk_http_header_t;
typedef void (*mtk_http_cb_t)(int, mtk_http_header_t*, size_t, const char*, size_t);
extern void mtk_start_http(const char *root_cert);
extern void mtk_stop_http();
extern void mtk_http_get(const char *host, const char *path,
                        mtk_http_header_t *headers, int n_headers,
                        mtk_http_cb_t cb);
extern void mtk_http_post(const char *host, const char *path,
                        mtk_http_header_t *headers, int n_headers,
						const char *body, int blen, 
                        mtk_http_cb_t cb);

#if defined(__cplusplus)
}
#endif
