#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* grpc server/client API */
typedef void *mtk_conn_t;
typedef void *mtk_svconn_t;
typedef void *mtk_reply_t;
typedef int mtk_result_t;
typedef void (*mtk_callback_t)(mtk_result_t, const char *, size_t);
typedef mtk_result_t (*mtk_server_callback_t)(mtk_svconn_t, mtk_result_t, const char *, size_t);
typedef struct {
	const char *host, *cert, *key, *ca;
} mtk_addr_t;
typedef struct {
	struct {
		uint32_t n_reader, n_writer;
	} thread;
	mtk_server_callback_t handler;
	bool exclusive; //if true, caller thread of mtk_listen blocks
	const char *listen_at;
	const char *root_cert;
} mtk_svconf_t;
typedef struct {
	uint64_t id;
	const char *payload;
	size_t payload_len;
	mtk_callback_t on_connect;
	bool (*validate)();
} mtk_clconf_t;
typedef enum {
	MTK_APPLICATION_ERROR = -1,
	MTK_TIMEOUT = -2,
} mtk_error_t;

/*  server */
extern void mtk_listen(mtk_addr_t *listen_at, mtk_svconf_t *conf);
extern mtk_svconn_t mtk_svconn_find(uint64_t id);
extern void mtk_svconn_id(mtk_svconn_t conn);
extern void mtk_svconn_send(mtk_svconn_t conn, const char *data, size_t datalen);
extern void mtk_svconn_add_task(mtk_svconn_t conn, const char *data, size_t datalen);
extern void mtk_svconn_close(mtk_svconn_t conn);
/* client */
extern mtk_conn_t mtk_connect(mtk_addr_t *connect_to, mtk_clconf_t *conf);
extern void mtk_conn_id(mtk_conn_t conn);
extern void mtk_conn_poll(mtk_conn_t conn);
extern void mtk_conn_close(mtk_conn_t conn);
extern void mtk_conn_send(mtk_conn_t conn, uint32_t type, const char *data, size_t datalen, mtk_callback_t cb);

/* http API */
typedef struct {
	char *key;
	char *value;
} mtk_http_header_t;
typedef void *mtk_http_server_request_t;
typedef void *mtk_http_server_response_t;
typedef void (*mtk_http_server_cb_t)(mtk_http_server_request_t, mtk_http_server_response_t);
typedef void (*mtk_http_client_cb_t)(int, mtk_http_header_t*, size_t, const char*, size_t);

extern void mtk_http_start(const char *root_cert);
extern bool mtk_http_listen(int port, mtk_http_server_cb_t cb);
extern void mtk_http_stop();
extern void mtk_http_get(const char *host, const char *path,
                        mtk_http_header_t *headers, int n_headers,
                        mtk_http_client_cb_t cb);
extern void mtk_http_post(const char *host, const char *path,
                        mtk_http_header_t *headers, int n_headers,
						const char *body, int blen, 
                        mtk_http_client_cb_t cb);
extern bool mtk_http_server_read_header(mtk_http_server_request_t *req, const char *key, char *value, size_t *size);
extern const char *mtk_http_server_read_body(mtk_http_server_request_t *req, size_t *size);
extern void mtk_http_server_write_header(mtk_http_server_response_t *res, mtk_http_header_t *hds, size_t n_hds);
extern void mtk_http_server_write_body(mtk_http_server_response_t *res, char *buffer, size_t len);

#if defined(__cplusplus)
}
#endif
