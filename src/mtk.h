#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* grpc server/client API */
typedef void *mtk_conn_t;
typedef void *mtk_svconn_t;
typedef int mtk_result_t;
typedef uint64_t mtk_cid_t;
typedef uint32_t mtk_msgid_t;
typedef void (*mtk_callback_t)(void *, mtk_result_t, const char *, size_t);
typedef bool (*mtk_connect_cb_t)(void *, mtk_cid_t, const char *, size_t);
typedef struct {
	void *arg;
	union {
		mtk_callback_t on_msg;
		mtk_connect_cb_t on_connect;
	};
} mtk_closure_t;
typedef mtk_result_t (*mtk_server_recv_cb_t)(mtk_svconn_t, mtk_result_t, const char *, size_t);
typedef mtk_cid_t (*mtk_server_accept_cb_t)(mtk_svconn_t, mtk_cid_t, const char *, size_t, char **, size_t*);
typedef struct {
	const char *host, *cert, *key, *ca;
} mtk_addr_t;
typedef struct {
	struct {
		uint32_t n_reader, n_writer;
	} thread;
	mtk_server_recv_cb_t handler;
	mtk_server_accept_cb_t acceptor;
	bool exclusive; //if true, caller thread of mtk_listen blocks
	const char *listen_at;
	const char *root_cert;
} mtk_svconf_t;
typedef struct {
	mtk_cid_t id;
	const char *payload;
	size_t payload_len;
	mtk_closure_t on_connect;
	bool (*validate)();
} mtk_clconf_t;
typedef enum {
	MTK_APPLICATION_ERROR = -1,
	MTK_TIMEOUT = -2,
	MTK_ACCEPT_DENY = -3,
} mtk_error_t;

/* server */
extern void mtk_listen(mtk_addr_t *listen_at, mtk_svconf_t *conf);
extern void mtk_join_server();
extern void mtk_svconn_accept(mtk_svconn_t conn, mtk_cid_t cid);
extern mtk_cid_t mtk_svconn_cid(mtk_svconn_t conn);
extern mtk_msgid_t mtk_svconn_msgid(mtk_svconn_t conn);
extern void mtk_svconn_send(mtk_svconn_t conn, mtk_msgid_t msgid, const char *data, size_t datalen);
extern void mtk_svconn_notify(mtk_svconn_t conn, uint32_t type, const char *data, size_t datalen);
extern void mtk_svconn_raise(mtk_svconn_t conn, mtk_msgid_t msgid, mtk_result_t errcode, const char *data, size_t datalen);
extern void mtk_svconn_task(mtk_svconn_t conn, uint32_t type, const char *data, size_t datalen);
extern void mtk_svconn_close(mtk_svconn_t conn);
extern void mtk_cid_send(mtk_cid_t cid, mtk_msgid_t msgid, const char *data, size_t datalen);
extern void mtk_cid_notify(mtk_cid_t cid, uint32_t type, const char *data, size_t datalen);
extern void mtk_cid_raise(mtk_cid_t cid, mtk_msgid_t msgid, mtk_result_t errcode, const char *errmsg);
extern void mtk_cid_task(mtk_cid_t cid, uint32_t type, const char *data, size_t datalen);
extern void mtk_cid_close(mtk_cid_t cid);
/* client */
extern mtk_conn_t mtk_connect(mtk_addr_t *connect_to, mtk_clconf_t *conf);
extern mtk_cid_t mtk_conn_cid(mtk_conn_t conn);
extern void mtk_conn_poll(mtk_conn_t conn);
extern void mtk_conn_close(mtk_conn_t conn);
extern void mtk_conn_send(mtk_conn_t conn, uint32_t type, const char *data, size_t datalen, mtk_closure_t clsr);
extern void mtk_conn_watch(mtk_conn_t conn, uint32_t type, mtk_closure_t clsr);
#define mtk_closure_init(__pclsr, __type, __cb, __arg) { \
	(__pclsr)->arg = (void *)(__arg); \
	(__pclsr)->__type = (__cb); \
}
#define mtk_closure_call(__pclsr, __type, ...) ((__pclsr)->__type((__pclsr)->arg, __VA_ARGS__))


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
extern void mtk_http_server_write_body(mtk_http_server_response_t *res, const char *buffer, size_t len);

/* utils */
typedef uint64_t mtk_time_t;
static inline mtk_time_t mtk_sec(int n) { return ((n) * 1000 * 1000 * 1000); }
static inline mtk_time_t mtk_msec(int n) { return ((n) * 1000 * 1000); }
static inline mtk_time_t mtk_usec(int n) { return ((n) * 1000); }
static inline mtk_time_t mtk_nsec(int n) { return (n); }
extern mtk_time_t mtk_time();
extern mtk_time_t mtk_sleep(mtk_time_t d);
extern void mtk_log_init();

#if defined(__cplusplus)
}
#endif
