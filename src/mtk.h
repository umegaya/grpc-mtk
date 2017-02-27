#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/******** basic types ********/
typedef void *mtk_server_t;
typedef void *mtk_conn_t;
typedef void *mtk_svconn_t;
typedef int mtk_result_t;
typedef uint64_t mtk_cid_t;
typedef uint32_t mtk_msgid_t;
typedef uint64_t mtk_time_t;
typedef uint32_t mtk_size_t;
typedef void *mtk_httpsrv_request_t;
typedef void *mtk_httpsrv_response_t;
typedef struct {
	char *key;
	char *value;
} mtk_http_header_t;
typedef void (*mtk_callback_t)(void *, mtk_result_t, const char *, mtk_size_t);
typedef bool (*mtk_connect_cb_t)(void *, mtk_cid_t, const char *, mtk_size_t);
typedef mtk_time_t (*mtk_close_cb_t)(void *, mtk_cid_t, long);
typedef mtk_result_t (*mtk_server_recv_cb_t)(void *, mtk_svconn_t, mtk_result_t, const char *, mtk_size_t);
typedef mtk_cid_t (*mtk_server_accept_cb_t)(void *, mtk_svconn_t, mtk_cid_t, const char *, mtk_size_t, char **, mtk_size_t*);
typedef void (*mtk_httpsrv_cb_t)(void *, mtk_httpsrv_request_t, mtk_httpsrv_response_t);
typedef void (*mtk_httpcli_cb_t)(void *, int, mtk_http_header_t*, mtk_size_t, const char*, mtk_size_t);
typedef struct {
	void *arg;
	union {
		mtk_callback_t on_msg;
		mtk_connect_cb_t on_connect;
		mtk_close_cb_t on_close;
		mtk_server_recv_cb_t on_svmsg;
		mtk_server_accept_cb_t on_accept;
		mtk_httpsrv_cb_t on_httpsrv;
		mtk_httpcli_cb_t on_httpcli;
		void *check;
	};
} mtk_closure_t;



/******** utils ********/
/* time */
static inline mtk_time_t mtk_sec(uint64_t n) { return ((n) * 1000 * 1000 * 1000); }
static inline mtk_time_t mtk_msec(uint64_t n) { return ((n) * 1000 * 1000); }
static inline mtk_time_t mtk_usec(uint64_t n) { return ((n) * 1000); }
static inline mtk_time_t mtk_nsec(uint64_t n) { return (n); }
extern mtk_time_t mtk_time();
extern mtk_time_t mtk_sleep(mtk_time_t d); //ignore EINTR
extern mtk_time_t mtk_pause(mtk_time_t d); //break with EINTR
/* log */
extern void mtk_log_init();



/******** grpc server/client API ********/
typedef struct {
	const char *host, *cert, *key, *ca;
} mtk_addr_t;
typedef struct {
	struct {
		uint32_t n_reader, n_writer;
	} thread;
	mtk_closure_t handler, acceptor;
	bool exclusive; //if true, caller thread of mtk_listen blocks
} mtk_svconf_t;
typedef struct {
	mtk_cid_t id;
	const char *payload;
	mtk_size_t payload_len;
	mtk_closure_t on_connect, on_close;
	bool (*validate)();
} mtk_clconf_t;
typedef enum {
	MTK_APPLICATION_ERROR = -1,
	MTK_TIMEOUT = -2,
	MTK_ACCEPT_DENY = -3,
} mtk_error_t;

/* server */
extern mtk_server_t mtk_listen(mtk_addr_t *listen_at, mtk_svconf_t *conf);
extern void mtk_listen_stop(mtk_server_t sv);
extern void mtk_svconn_accept(mtk_svconn_t conn, mtk_cid_t cid);
extern mtk_cid_t mtk_svconn_cid(mtk_svconn_t conn);
extern mtk_msgid_t mtk_svconn_msgid(mtk_svconn_t conn);
extern void mtk_svconn_send(mtk_svconn_t conn, mtk_msgid_t msgid, const char *data, mtk_size_t datalen);
extern void mtk_svconn_notify(mtk_svconn_t conn, uint32_t type, const char *data, mtk_size_t datalen);
extern void mtk_svconn_error(mtk_svconn_t conn, mtk_msgid_t msgid, const char *data, mtk_size_t datalen);
extern void mtk_svconn_task(mtk_svconn_t conn, uint32_t type, const char *data, mtk_size_t datalen);
extern void mtk_svconn_close(mtk_svconn_t conn);
extern void mtk_cid_send(mtk_cid_t cid, mtk_msgid_t msgid, const char *data, mtk_size_t datalen);
extern void mtk_cid_notify(mtk_cid_t cid, uint32_t type, const char *data, mtk_size_t datalen);
extern void mtk_cid_error(mtk_cid_t cid, mtk_msgid_t msgid, const char *data, mtk_size_t datalen);
extern void mtk_cid_task(mtk_cid_t cid, uint32_t type, const char *data, mtk_size_t datalen);
extern void mtk_cid_close(mtk_cid_t cid);
/* client */
extern mtk_conn_t mtk_connect(mtk_addr_t *connect_to, mtk_clconf_t *conf);
extern mtk_cid_t mtk_conn_cid(mtk_conn_t conn);
extern void mtk_conn_poll(mtk_conn_t conn);
extern void mtk_conn_close(mtk_conn_t conn);
extern void mtk_conn_reset(mtk_conn_t conn); //this just restart connection, never destroy. 
extern void mtk_conn_send(mtk_conn_t conn, uint32_t type, const char *data, mtk_size_t datalen, mtk_closure_t clsr);
extern void mtk_conn_watch(mtk_conn_t conn, uint32_t type, mtk_closure_t clsr);
extern bool mtk_conn_connected(mtk_svconn_t conn);
#define mtk_closure_init(__pclsr, __type, __cb, __arg) { \
	(__pclsr)->arg = (void *)(__arg); \
	(__pclsr)->__type = (__cb); \
}
extern mtk_closure_t mtk_closure_nop;
#define mtk_closure_valid(__pclsr) ((__pclsr)->check != nullptr)
#define mtk_closure_call(__pclsr, __type, ...) ((__pclsr)->__type((__pclsr)->arg, __VA_ARGS__))



/******** http API ********/
/* common */
extern void mtk_http_start(const char *root_cert);
extern void mtk_http_stop();
extern bool mtk_http_avail();
/* client */
extern void mtk_httpcli_get(const char *host, const char *path, mtk_http_header_t *hds, int n_hds, mtk_closure_t cb);
extern void mtk_httpcli_post(const char *host, const char *path, mtk_http_header_t *hds, int n_hds, 
						const char *body, int blen, mtk_closure_t cb);
extern void mtk_httpcli_get_insecure(const char *host, const char *path, mtk_http_header_t *hds, int n_hds, mtk_closure_t cb);
extern void mtk_httpcli_post_insecure(const char *host, const char *path, mtk_http_header_t *hds, int n_hds, 
						const char *body, int blen, mtk_closure_t cb);
/* server */
extern bool mtk_httpsrv_listen(int port, mtk_closure_t cb);
extern const char *mtk_httpsrv_read_path(mtk_httpsrv_request_t req, char *value, mtk_size_t *size);
extern const char *mtk_httpsrv_read_header(mtk_httpsrv_request_t req, const char *key, char *value, mtk_size_t *size);
extern const char *mtk_httpsrv_read_body(mtk_httpsrv_request_t req, mtk_size_t *size);
extern void mtk_httpsrv_write_header(mtk_httpsrv_response_t res, int status, mtk_http_header_t *hds, mtk_size_t n_hds);
extern void mtk_httpsrv_write_body(mtk_httpsrv_response_t res, const char *buffer, mtk_size_t len);

#if defined(__cplusplus)
}
#endif
