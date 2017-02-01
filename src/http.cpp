//this file is shared... so please not include client specific headers (eg. for TRACE)
#include "http.h"
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/sync.h>
#include <grpc/impl/codegen/alloc.h>
extern "C" {
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/polling_entity.h"
}
#include <string>
#include <memory.h>
#include <stdlib.h>

#include <thread>

static gpr_mu *g_polling_mu;
static grpc_pollset *g_pollset = nullptr;
static grpc_exec_ctx g_exec_ctx = GRPC_EXEC_CTX_INIT;

static std::string g_root_cert;
static std::thread g_webthr;
static bool g_http_alive = false;

namespace mtk {

    //requester
    typedef struct _RequestContext {
        grpc_httpcli_context context_;
        grpc_polling_entity pollent_;
        grpc_http_response response_;
        grpc_closure closure_;
        HttpClient::Callback cb_;
        void Init(const char *host, const char *path,
                  grpc_http_header *headers, int n_headers,
                  const char *body, int blen,
                  HttpClient::Callback cb, bool ssl, uint32_t timeout_msec) {
            gpr_timespec timeout = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                                gpr_time_from_millis(timeout_msec, GPR_TIMESPAN));
            pollent_ = grpc_polling_entity_create_from_pollset(g_pollset);
            grpc_closure_init(&closure_, _RequestContext::tranpoline, this);
            memset(&response_, 0, sizeof(response_));
            cb_ = cb;
            
            grpc_httpcli_request req;
            memset(&req, 0, sizeof(grpc_httpcli_request));
            req.host = (char *)host;
            req.handshaker = ssl ? &grpc_httpcli_ssl : NULL/* to use default */;
            req.http.path = (char *)path;
            req.http.hdrs = headers;
            req.http.hdr_count = n_headers;
            
            grpc_httpcli_context_init(&context_);
            if (body != nullptr) {
                grpc_httpcli_post(&g_exec_ctx, &context_, &pollent_, &req,
                                  body, blen,
                                  timeout, &closure_, &response_);
            } else {
                grpc_httpcli_get(&g_exec_ctx, &context_, &pollent_, &req,
                                 timeout, &closure_, &response_);
            }
        }
        void Fin() {
            grpc_httpcli_context_destroy(&context_);
            grpc_http_response_destroy(&response_);
        }
        static void tranpoline(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error) {
            _RequestContext *ctx = (_RequestContext *)arg;
            if (ctx->closure_.error != nullptr) {
                //g_logger->error("tag:http,ev:request fail,emsg:{}", grpc_error_string(ctx->closure_.error));
            }
            grpc_http_response &resp = ctx->response_;
            ctx->cb_(resp.status, resp.hdrs, resp.hdr_count, resp.body, resp.body_length);
            ctx->Fin();
            delete ctx;
        }
    } RequestContext;

    static grpc_ssl_roots_override_result pemer(char **pem) {
        *pem = (char *)g_root_cert.c_str();
        return GRPC_SSL_ROOTS_OVERRIDE_OK;
    }
    void HttpClient::Init(const std::string &root_cert) {
        g_root_cert = root_cert;
        if (g_pollset == nullptr) {
            g_pollset = (grpc_pollset *)malloc(grpc_pollset_size());
            grpc_pollset_init(g_pollset, &g_polling_mu);
            grpc_set_ssl_roots_override_callback(pemer);
        }
    }
    static void destroy_pollset(grpc_exec_ctx *exec_ctx, void *p, grpc_error *e) {
        grpc_pollset_destroy((grpc_pollset *)p);
    }
    void HttpClient::Fin() {
        if (g_pollset != nullptr) {
            grpc_closure destroy_closure;
            grpc_closure_init(&destroy_closure, destroy_pollset, g_pollset);
            grpc_pollset_shutdown(&g_exec_ctx,
                                  g_pollset,
                                  &destroy_closure);
            free(g_pollset);
            grpc_exec_ctx_finish(&g_exec_ctx);
            g_pollset = nullptr;
        }
    }
    void HttpClient::Update(int sleep_ms) {
        grpc_pollset_worker *worker = NULL;
        gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
        gpr_mu_lock(g_polling_mu);
        GRPC_LOG_IF_ERROR("pollset_work",
                          grpc_pollset_work(&g_exec_ctx, g_pollset,
                                            &worker, now,
                                            gpr_time_add(now, gpr_time_from_millis(sleep_ms, GPR_TIMESPAN))));
        gpr_mu_unlock(g_polling_mu);
    }

    void HttpClient::Get(const char *host, const char *path,
                         grpc_http_header *headers, int n_headers,
                         Callback cb, bool ssl,
                         uint32_t timeout_msec) {
        RequestContext *ctx = new RequestContext();
        ctx->Init(host, path, headers, n_headers, NULL, 0, cb, ssl, timeout_msec);
    }

    void HttpClient::Post(const char *host, const char *path,
                          grpc_http_header *headers, int n_headers,
                          const char *body, int blen,
                          Callback cb, bool ssl,
                          uint32_t timeout_msec) {
        RequestContext *ctx = new RequestContext();
        ctx->Init(host, path, headers, n_headers, body, blen, cb, ssl, timeout_msec);
    }
    void HttpClient::Start(const std::string &root_cert) {
        if (g_http_alive) {
            return;
        }
        g_webthr = std::move(std::thread([&root_cert] {
            g_http_alive = true;
            HttpClient::Init(root_cert);
            while (g_http_alive) {
                HttpClient::Update();
            }
            HttpClient::Fin();
        }));
    }
    void HttpClient::Stop() {
        if (!g_http_alive) {
            return;
        }
        g_http_alive = false;
        g_webthr.join();
    }
}

