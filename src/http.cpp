//this file is shared... so please not include client specific headers (eg. for TRACE)
#include "http.h"
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/sync.h>
extern "C" {
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/resolve_address.h"
}
#include <memory.h>
#include <stdlib.h>

#include <thread>

#include "debug.h"

static gpr_mu *g_polling_mu;
static grpc_pollset *g_pollset = nullptr;
static grpc_exec_ctx g_exec_ctx = GRPC_EXEC_CTX_INIT;

static std::string g_root_cert;
static std::thread g_webthr;
static bool g_http_alive = false;

#define EXPAND_BUFFER

namespace mtk {

    /*******  HttpClient *******/
    typedef struct _RequestContext {
        grpc_httpcli_context context_;
        grpc_polling_entity pollent_;
        grpc_httpcli_response response_;
        grpc_closure closure_;
        HttpClient::Callback cb_;
        void Init(const char *host, const char *path,
                  grpc_http_header *headers, int n_headers,
                  const char *body, int blen,
                  HttpClient::Callback cb, bool ssl, uint32_t timeout_msec) {
            gpr_timespec timeout = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                                gpr_time_from_millis(timeout_msec, GPR_TIMESPAN));
            pollent_ = grpc_polling_entity_create_from_pollset(g_pollset);
            grpc_closure_init(&closure_, _RequestContext::tranpoline, this, grpc_schedule_on_exec_ctx);
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
            grpc_resource_quota *resource_quota = grpc_resource_quota_create(NULL);
            if (body != nullptr) {
                grpc_httpcli_post(&g_exec_ctx, &context_, &pollent_, resource_quota, 
                                  &req, body, blen,
                                  timeout, &closure_, &response_);
            } else {
                grpc_httpcli_get(&g_exec_ctx, &context_, &pollent_, resource_quota, 
                                 &req,
                                 timeout, &closure_, &response_);
            }
            grpc_resource_quota_unref_internal(&g_exec_ctx, resource_quota);
        }
        void Fin() {
            grpc_httpcli_context_destroy(&context_);
            grpc_http_response_destroy(&response_);
            GRPC_LOG_IF_ERROR(
                "pollset_kick",
                grpc_pollset_kick(grpc_polling_entity_pollset(&pollent_), NULL));
        }
        static void tranpoline(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error) {
            _RequestContext *ctx = (_RequestContext *)arg;
            if (ctx->closure_.error_data.error != nullptr) {
                LOG(error, "tag:http,ev:request fail,emsg:{}", grpc_error_string(ctx->closure_.error_data.error));
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
            grpc_closure_init(&destroy_closure, destroy_pollset, g_pollset, grpc_schedule_on_exec_ctx);
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
        g_webthr = std::thread([&root_cert] {
            g_http_alive = true;
            HttpClient::Init(root_cert);
            while (g_http_alive) {
                HttpClient::Update();
            }
            HttpClient::Fin();
        });
    }
    void HttpClient::Stop() {
        if (!g_http_alive) {
            return;
        }
        g_http_alive = false;
        g_webthr.join();
    }



    /******* HttpFSM functions *******/
    void
    HttpFSM::reset(uint32_t chunk_size)
    {
        m_buf = m_p = (char *)malloc(chunk_size);
        ASSERT(m_p != nullptr);
        m_len = 0;
        m_max = chunk_size;
        m_ctx.version = version_1_1;
        m_ctx.n_hd = 0;
        m_ctx.bd = nullptr;
        m_ctx.state = state_recv_header;
    }

    HttpFSM::state
    HttpFSM::append(char *b, int bl)
    {
        //  TRACE("append %u byte <%s>\n", bl, b);
        state s = get_state();
        char *w = b;
        uint32_t limit = (m_max - 1);
        while (s != state_error && s != state_recv_finish) {
            if (m_len >= limit) {
    #if defined(EXPAND_BUFFER)
                //try expand buffer
                char *org = m_p;
                m_p = (char *)realloc(m_p, limit * 2);
                m_buf = m_p + (m_buf - org);
                for (int i = 0; i < m_ctx.n_hd; i++) {
                    m_ctx.hd[i] = m_p + (m_ctx.hd[i] - org);
                }
                if (m_ctx.bd != nullptr) {
                    m_ctx.bd = m_p + (m_ctx.bd - org);
                }
    #else
                s = state_error;
                break;
    #endif
            }
            m_p[m_len++] = *w++;
            m_p[m_len] = '\0';
    #if defined(_DEBUG)
            //      if ((m_len % 100) == 0) { TRACE("."); }
            //      TRACE("recv[%u]:%u\n", m_len, s);
    #endif
            switch(s) {
                case state_recv_header:
                    s = recv_header(); break;
                case state_recv_body:
                    s = recv_body(); break;
                case state_recv_body_nochunk:
                    s = recv_body_nochunk(); break;
                case state_recv_bodylen:
                    s = recv_bodylen(); break;
                case state_recv_footer:
                    s = recv_footer(); break;
                case state_recv_comment:
                    s = recv_comment(); break;
                case state_websocket_establish:
                    goto end;
                default:
                    break;
            }
            if ((w - b) >= bl) { break; }
        }
    end:
        recvctx().state = (uint16_t)s;
        return s;
    }

    char*
    HttpFSM::hdrstr(const char *key, char *b, int l, int *outlen) const
    {
        for (int i = 0; i < m_ctx.n_hd; i++) {
            const char *k = key;
            const char *p = m_ctx.hd[i];
            /* key name comparison by case non-sensitive */
            while (*k && tolower(*k) == tolower(*p)) {
                if ((k - key) > m_ctx.hl[i]) {
                    ASSERT(false);
                    return NULL;    /* key name too long */
                }
                k++; p++;
            }
            if (*k) {
                continue;   /* key name and header tag not match */
            }
            else {
                /* seems header is found */
                while (*p) {
                    /* skip [spaces][:][spaces] between [tag] and [val] */
                    if (*p == ' ' || *p == ':') { p++; }
                    else { break; }
                    if ((m_ctx.hd[i] - p) > m_ctx.hl[i]) {
                        ASSERT(false);
                        return NULL;    /* too long space(' ') */
                    }
                }
                char *w = b;
                while (*p) {
                    *w++ = *p++;
                    if ((w - b) >= l) {
                        ASSERT(false);
                        return NULL;    /* too long header paramter */
                    }
                }
                if (outlen) {
                    *outlen = (int)(w - b);
                }
                *w = 0; /* null terminate */
                return b;
            }
        }
        return NULL;
    }

    bool
    HttpFSM::hdrint(const char *key, int &out) const
    {
        char b[256];
        if (NULL != hdrstr(key, b, sizeof(b))) {
            try {
                size_t idx;
                out = std::stoi(b, &idx);
                if (b[idx] != 0) {
                    return false;
                }
            } catch (std::exception &e) {
                return false;
            }
            return true;
        }
        return false;
    }

    int
    HttpFSM::recv_lf() const
    {
        const char *p = current();
        //  if (m_len > 1) {
        //      TRACE("now last 2byte=<%s:%u>%u\n", (p - 2), GET_16(p - 2), htons(crlf));
        //  }
        if (m_len > 2 && GET_16(p - 2) == htons(crlf)) {
            return 2;
        }
        if (m_len > 1 && *(p - 1) == '\n') {
            return 1;
        }
        return 0;
    }

    int
    HttpFSM::recv_lflf() const
    {
        const char *p = current();
        if (m_len > 4 && GET_32(p - 4) == htonl(crlfcrlf)) {
            return 4;
        }
        if (m_len > 2 && GET_16(p - 2) == htons(lflf)) {
            return 2;
        }
        return 0;
    }

    HttpFSM::state
    HttpFSM::recv_header()
    {
        char *p = current();
        int nlf, tmp;
        if ((nlf = recv_lf())) {
            /* lf found but line is empty. means \n\n or \r\n\r\n */
            tmp = nlf;
            for (;tmp > 0; tmp--) {
                *(p - tmp) = '\0';
            }
            if ((p - nlf) == m_buf) {
                int cl; char tok[256];
                /* get result code */
                m_ctx.res = putrc();
                /* if content length is exist, no chunk encoding */
                if (hdrint("Content-Length", cl)) {
                    recvctx().bd = p;
                    recvctx().bl = cl;
                    return state_recv_body_nochunk;
                }
                /* if chunk encoding, process as chunk */
                else if (hdrstr("Transfer-Encoding", tok, sizeof(tok)) != NULL &&
                         memcmp(tok, "chunked", sizeof("chunked") - 1) == 0) {
                    m_buf = recvctx().bd = p;
                    recvctx().bl = 0;
                    return state_recv_bodylen;
                }
                else if (hdrstr("Sec-WebSocket-Key", tok, sizeof(tok)) ||
                         hdrstr("Sec-WebSocket-Accept", tok, sizeof(tok))) {
                    return state_websocket_establish;
                }
                else if (rc() == HRC_OK){
                    return state_error;
                }
                else { return state_recv_finish; }
            }
            /* lf found. */
            else if (recvctx().n_hd < MAX_HEADER) {
                recvctx().hd[recvctx().n_hd] = m_buf;
                recvctx().hl[recvctx().n_hd] = (p - m_buf) - nlf;
                m_buf = p;
                recvctx().n_hd++;
            }
            else {  /* too much header. */
                return state_error;
            }
        }
        return state_recv_header;
    }

    HttpFSM::state
    HttpFSM::recv_body()
    {
        int nlf;
        if ((nlf = recv_lf())) {
            /* some stupid web server contains \n in its response...
             * so we check actual length is received */
            long n_diff = (recvctx().bd + recvctx().bl) - (m_p + m_len - nlf);
            if (n_diff > 0) {
                /* maybe \r\n will come next */
                return state_recv_body;
            }
            else if (n_diff < 0) {
                /* it should not happen even if \n is contained */
                return state_error;
            }
            m_len -= nlf;
            m_buf = current();
            return state_recv_bodylen;
        }
        return state_recv_body;
    }

    HttpFSM::state
    HttpFSM::recv_body_nochunk()
    {
        long diff = (recvctx().bd + recvctx().bl) - (m_p + m_len);
        if (diff > 0) {
            return state_recv_body_nochunk;
        }
        else if (diff < 0) {
            return state_error;
        }
        return state_recv_finish;
    }

    HttpFSM::state
    HttpFSM::recv_bodylen()
    {
        char *p = current();
        state s = state_recv_bodylen;
        
        int nlf;
        if ((nlf = recv_lf())) {
            s = state_recv_body;
        }
        else if (*p == ';') {
            /* comment is specified after length */
            nlf = 1;
            s = state_recv_comment;
        }
        if (s != state_recv_bodylen) {
            int cl;
            for (;nlf > 0; nlf--) {
                *(p - nlf) = '\0';
            }
            if (!htoi(m_buf, &cl, (p - m_buf))) {
                return state_error;
            }
            /* 0-length chunk means chunk end -> next footer */
            if (cl == 0) {
                m_buf = p;
                return state_recv_footer;
            }
            recvctx().bl += cl;
            m_len -= (p - m_buf);
        }
        return s;
    }

    HttpFSM::state
    HttpFSM::recv_footer()
    {
        char *p = current();
        int nlf, tmp;
        if ((nlf = recv_lf())) {
            tmp = nlf;
            for (;tmp > 0; tmp--) {
                *(p - tmp) = '\0';
            }
            /* lf found but line is empty. means \n\n or \r\n\r\n */
            if ((p - nlf) == m_buf) {
                return state_recv_finish;
            }
            /* lf found. */
            else if (recvctx().n_hd < MAX_HEADER) {
                recvctx().hd[recvctx().n_hd] = m_buf;
                recvctx().hl[recvctx().n_hd] = (p - m_buf) - nlf;
                *p = '\0';
                m_buf = p;
                recvctx().n_hd++;
            }
            else {  /* too much footer + header. */
                return state_error;
            }
        }
        return state_recv_footer;
    }

    HttpFSM::state
    HttpFSM::recv_comment()
    {
        int nlf;
        if ((nlf = recv_lf())) {
            char *p = current();
            m_len -= (p - m_buf);
            return state_recv_body;
        }
        return state_recv_comment;
    }

    const char *
    HttpFSM::url(char *b, int l, size_t *p_out)
    {
        const char *w = m_ctx.hd[0];
        /* skip first spaces */
        while (*w != ' ') {
            w++;
            if ((w - m_ctx.hd[0]) > m_ctx.hl[0]) {
                return nullptr;
            }
            /* reach to end of string: format error */
            if (*w == '\0') { return nullptr; }
        }
        w++;
        if (*w == '/') { w++; }
        char *wb = b;
        while (*w != ' ') {
            *wb++ = *w++;
            if ((wb - b) > l) {
                return nullptr;
            }
            if (*w == '\0') { return nullptr; }
        }
        *wb = '\0';
        if (p_out != nullptr) {
            *p_out = (wb - b);
        }
        return b;
    }

    bool
    HttpFSM::htoi(const char* str, int *i, size_t max)
    {
        const char *_s = str;
        int minus = 0;
        *i = 0;
        if ('-' == *_s) {
            minus = 1;
            _s++;
        }
        while(*_s) {
            int num = -1;
            if ('0' <= *_s && *_s <= '9') {
                num = (int)((*_s) - '0');
            }
            if ('a' <= *_s && *_s <= 'f') {
                num = (int)(((*_s) - 'a') + 10);
            }
            if ('A' <= *_s && *_s <= 'F') {
                num = (int)(((*_s) - 'A') + 10);
            }
            if (num < 0) {
                return false;
            }
            (*i) = (*i) * 16 + num;
            _s++;
            if (_s - str >= max) {
                return false;
            }
        }
        
        if (minus) {
            (*i) = -1 * (*i);
        }
        
        return true;
    }

    bool
    HttpFSM::atoi(const char* str, int *i, size_t max)
    {
        const char *_s = str;
        int minus = 0;
        *i = 0;
        if ('-' == *_s) {
            minus = 1;
            _s++;
        }
        while(*_s) {
            if ('0' <= *_s && *_s <= '9') {
                (*i) = (*i) * 10 + (int)((*_s) - '0');
            }
            else {
                return false;
            }
            _s++;
            if (_s - str >= max) {
                return false;
            }
        }
        
        if (minus) {
            (*i) = -1 * (*i);
        }
        
        return true;
    }

    HttpFSM::result_code
    HttpFSM::putrc()
    {
        const char *w = m_ctx.hd[0], *s = w;
        w += 5; /* skip first 5 character (HTTP/) */
        if (memcmp(w, "1.1", sizeof("1.1") - 1) == 0) {
            m_ctx.version = 11;
            w += 3;
        }
        else if (memcmp(w, "1.0", sizeof("1.0") - 1) == 0) {
            m_ctx.version = 10;
            w += 3;
        }
        else {
            return HRC_ERROR;
        }
        char tok[256];
        char *t = tok;
        while(*w) {
            w++;
            if (*w != ' ') { break; }
            if ((w - s) > m_ctx.hl[0]) {
                return HRC_ERROR;
            }
        }
        while(*w) {
            if (*w == ' ') { break; }
            *t++ = *w++;
            if ((w - s) > m_ctx.hl[0]) {
                return HRC_ERROR;
            }
            if ((unsigned int )(t - tok) >= sizeof(tok)) {
                return HRC_ERROR;
            }
        }
        int sc;
        *t = '\0';
        if (!atoi(tok, &sc, sizeof(tok))) {
            return HRC_ERROR;
        }
        return (result_code)sc;
    }

    bool HttpFSM::hdr_contains(const char *header_name, const char *content) const
    {
        int hdlen;
        char buffer[256];
        const char *value = hdrstr(header_name, buffer, sizeof(buffer), &hdlen);
        if (value != nullptr) {
            if (strstr(buffer, content) != nullptr) {
                return true;
            } else {
                return false;
            }
        }
        //if no header found, regard peer as can accept anything.
        return true;
    }



    /******* HttpServeContext *******/
    class HttpServContext : public HttpServer::IResponseWriter {
        typedef HttpFSM::result_code result_code;
    public:
        HttpServContext(grpc_endpoint *sock, HttpServer::Callback cb) : sock_(sock), cb_(cb), fsm_() {
            grpc_closure_init(&on_read_, _OnRead, this, grpc_schedule_on_exec_ctx);
            grpc_closure_init(&on_write_, _OnWrite, this, grpc_schedule_on_exec_ctx);
            gpr_slice_buffer_init(&buffer_);
            gpr_slice_buffer_init(&body_);
            FSM().reset(512);
        }
        virtual ~HttpServContext() {}
        inline HttpFSM &FSM() { return fsm_; }
        void Close(grpc_exec_ctx *exec_ctx) {
            gpr_slice_buffer_destroy(&buffer_);
            gpr_slice_buffer_destroy(&body_);
            grpc_endpoint_destroy(exec_ctx, sock_);
        }
        void Read(grpc_exec_ctx *exec_ctx) {
            grpc_endpoint_read(exec_ctx, sock_, &buffer_, &on_read_);
        }
        void Write(grpc_exec_ctx *exec_ctx) {
            grpc_endpoint_write(exec_ctx, sock_, &buffer_, &on_write_);
        }
        void OnWrite(grpc_exec_ctx *exec_ctx, grpc_error *error) {
            //write finished.
            Close(exec_ctx);
            delete this; //cannot touch this after here
        }
        void OnRead(grpc_exec_ctx *exec_ctx, grpc_error *error) {
            if (error == GRPC_ERROR_NONE) {
                switch (FSM().get_state()) {
                case HttpFSM::state_recv_header:
                case HttpFSM::state_recv_body:
                case HttpFSM::state_recv_body_nochunk:
                case HttpFSM::state_recv_bodylen:
                case HttpFSM::state_recv_footer:
                case HttpFSM::state_recv_comment:
                    while (buffer_.length > 0) {
                        auto s = gpr_slice_buffer_take_first(&buffer_);
                        FSM().append((char *)GPR_SLICE_START_PTR(s), (int)GPR_SLICE_LENGTH(s));
                        gpr_slice_unref(s);
                    }
                    if (FSM().get_state() == HttpFSM::state_recv_finish) {
                        cb_(FSM(), *this);
                        Write(exec_ctx); //write start
                    } else {
                        Read(exec_ctx); //read again
                    }
                    return; //not close connection
                case HttpFSM::state_recv_finish:
                case HttpFSM::state_invalid:
                case HttpFSM::state_error:
                default:
                    ASSERT(false);
                    break;
                }
            }
            Close(exec_ctx);
            delete this; //cannot touch this after here
        }
        //response writer
        void WriteHeader(result_code rc, grpc_http_header *headers, int n_header) {
            gpr_slice_buffer_add(&buffer_, MemFmt("HTTP/1.1 %d\r\n", 64, rc));
            for (int i = 0; i < n_header; i++) {
                const char *fmt = (i == (n_header - 1) ? "%s: %s\r\n\r\n" : "%s: %s\r\n");
                gpr_slice_buffer_add(&buffer_, MemFmt(fmt, 1024, headers[i].key, headers[i].value));
            }
        }
        void WriteBody(const uint8_t *p, size_t l) {
            //TODO: now we let grpc runtime to flow control of large body (~1M).
            //but if it not works well, need to use body_ and do flow control of our own
            gpr_slice_buffer_add(&buffer_, MemDupe(p, l));
        }
    protected:
        static void _OnRead(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
            ((HttpServContext *)arg)->OnRead(exec_ctx, error);
        }
        static void _OnWrite(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
            ((HttpServContext *)arg)->OnWrite(exec_ctx, error);
        }
        static gpr_slice MemDupe(const void *p, size_t l) {
            void *dst = malloc(l);
            memcpy(dst, p, l);
            return gpr_slice_new(dst, l, free);
        }
        static gpr_slice MemFmt(const char *fmt, size_t max, ...) {
            va_list v;
            va_start(v, max);
            char b[max];
            size_t ret = vsnprintf(b, max, fmt, v);
            void *p = malloc(ret);
            memcpy(p, b, ret);
            va_end(v);
            return gpr_slice_new(p, ret, free);
        }
        grpc_endpoint *sock_;
        gpr_slice_buffer buffer_, body_;
        grpc_closure on_read_, on_write_;
        HttpServer::Callback cb_;
        HttpFSM fsm_;
    };



    /******* HttpServer *******/
    HttpServer *HttpServer::instance_ = nullptr;

    HttpServer &HttpServer::Instance() {
        if (instance_ == nullptr) {
            instance_ = new HttpServer();
        }
        return *instance_;
    }

    HttpServer::HttpServer() {
        n_listeners_capacity_ = 4;
        n_listeners_ = 0;
        alive_ = true;
        listeners_ = new ListenerEntry[n_listeners_capacity_];
    }

    bool HttpServer::Init() {
        pollset_ = (grpc_pollset *)malloc(grpc_pollset_size());
        grpc_pollset_init(pollset_, &polling_mu_);
        grpc_closure_init(&on_destroy_, &HttpServer::_OnDestroy, this, grpc_schedule_on_exec_ctx);
        return true;
    }
    void HttpServer::Fin() {
        alive_ = false;
    }
    bool HttpServer::DoListen(grpc_exec_ctx *exec_ctx, int port) {
        grpc_resolved_addresses *resolved = nullptr;
        grpc_blocking_resolve_address(("0.0.0.0:" + std::to_string(port)).c_str(), "https", &resolved);
        if (resolved == nullptr) {
            LOG(info, "ev:cannot listen,addr:{}", (":" + std::to_string(port)).c_str());
            return false;
        }
        const size_t naddrs = resolved->naddrs;
        for (int i = 0; i < naddrs; i++) {
            int tmp;
            auto err = grpc_tcp_server_add_port(server_, &resolved->addrs[i], &tmp);
            if (GRPC_ERROR_NONE != err) {
                GRPC_ERROR_UNREF(err);
                return false;
            }
            if (port != tmp) {
                return false;
            }
        }
        return true;
    }
    bool HttpServer::Listen(int port, Callback cb) {
        AddHandler(port, cb);
        return true;
    }
    bool HttpServer::Run(int sleep_ms) {
        bool alive = true;
        grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
        auto err = grpc_tcp_server_create(&exec_ctx, &on_destroy_, nullptr, &server_);
        if (err != GRPC_ERROR_NONE) {
            GRPC_ERROR_UNREF(err);
            ASSERT(false);
            return false;
        }
        for (int i = 0; i < n_listeners_; i++) {
            if (!DoListen(&exec_ctx, listeners_[i].port)) {
                ASSERT(false);
                return false;
            }
        }
        grpc_tcp_server_ref(server_);
        grpc_tcp_server_start(&exec_ctx, server_, &pollset_, 1, &HttpServer::_OnAccept, this);
        while (alive) {
            grpc_pollset_worker *worker = NULL;
            gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
            gpr_mu_lock(polling_mu_);
            GRPC_LOG_IF_ERROR("pollset_work",
                grpc_pollset_work(&exec_ctx, pollset_, &worker, now,
                gpr_time_add(now, gpr_time_from_millis(sleep_ms, GPR_TIMESPAN))));
            gpr_mu_unlock(polling_mu_);
        }
        //finish.
        grpc_tcp_server_unref(&exec_ctx, server_);
        return true;
    }

    void HttpServer::OnDestroy(grpc_exec_ctx *exec_ctx, grpc_error *error) {
        
    }
    void HttpServer::OnAccept(grpc_exec_ctx *exec_ctx, grpc_endpoint *tcp, grpc_pollset *accepting_pollset,
                              grpc_tcp_server_acceptor *acceptor) {
        auto ctx = new HttpServContext(tcp, listeners_[acceptor->port_index].callback);
        ctx->Read(exec_ctx);
    }

    void HttpServer::AddHandler(int port, Callback cb) {
        if (n_listeners_ == n_listeners_capacity_) {
            n_listeners_capacity_ *= 2;
            ListenerEntry *new_handers = new ListenerEntry[n_listeners_capacity_];
            for (int i = 0; i < n_listeners_; i++) {
                new_handers[i] = listeners_[i];
            }
            delete []listeners_;
            listeners_ = new_handers;
        }
        ASSERT(n_listeners_ < n_listeners_capacity_);
        listeners_[n_listeners_].port = port;
        listeners_[n_listeners_++].callback = cb;
    }

    void HttpServer::IResponseWriter::WriteResponse(const uint8_t *p, size_t l) {
        grpc_http_header hds = {
            .key=(char *)"Content-Length", .value=(char *)std::to_string(l).c_str()
        };
        WriteHeader(HRC_OK, &hds, 1);
        WriteBody(p, l);
    }

}

