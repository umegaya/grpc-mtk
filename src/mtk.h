#pragma once

#if defined(__cplusplus)
extern "C" {
#endif

typedef void *mtk_server_t;
typedef void *mtk_conn_t;
typedef void *mtk_svconn_t;
typedef void *mtk_reply_t;
typedef int mtk_result_t;
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
	const char *listen_at;
} mtk_svconf_t;
typedef struct {
	const char *connect_payload;
	size_t connect_payload_len;
	mtk_callback_t login_cb;
	mtk_valid_checker_t valid_cb;
} mtk_clconf_t;
typedef enum {
	MTK_APPLICATION_ERROR = -1,
	MTK_TIMEOUT = -2,
} mtk_error_t;

extern mtk_server_t mtk_listen(mtk_addr_t *, mtk_svconf_t*);
extern mtk_conn_t mtk_connect(mtk_addr_t *, mtk_clconf_t*);
extern void mtk_conn_poll(mtk_conn_t);
extern void mtk_close(mtk_conn_t);
extern void mtk_send(mtk_conn_t, uint32_t, const char *, size_t, mtk_callback_t);

#if defined(__cplusplus)
}
#endif
