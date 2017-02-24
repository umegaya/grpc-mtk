#pragma once

#include <functional>
#include <string>
#include <map>
extern "C" {
#include "src/core/lib/http/parser.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/iomgr/closure.h"
}

namespace mtk {
    /****** HTTP status codes *******/
    typedef enum
    {
        HRC_ERROR = -1,         /* An error response from httpXxxx() */
        
        HRC_CONTINUE = 100,         /* Everything OK, keep going... */
        HRC_SWITCHING_PROTOCOLS,        /* HRC upgrade to TLS/SSL */
        
        HRC_OK = 200,           /* OPTIONS/GET/HEAD/POST/TRACE command was successful */
        HRC_CREATED,                /* PUT command was successful */
        HRC_ACCEPTED,           /* DELETE command was successful */
        HRC_NOT_AUTHORITATIVE,      /* Information isn't authoritative */
        HRC_NO_CONTENT,         /* Successful command, no new data */
        HRC_RESET_CONTENT,          /* Content was reset/recreated */
        HRC_PARTIAL_CONTENT,            /* Only a partial file was recieved/sent */
        
        HRC_MULTIPLE_CHOICES = 300,     /* Multiple files match request */
        HRC_MOVED_PERMANENTLY,      /* Document has moved permanently */
        HRC_MOVED_TEMPORARILY,      /* Document has moved temporarily */
        HRC_SEE_OTHER,          /* See this other link... */
        HRC_NOT_MODIFIED,           /* File not modified */
        HRC_USE_PROXY,          /* Must use a proxy to access this URI */
        
        HRC_BAD_REQUEST = 400,      /* Bad request */
        HRC_UNAUTHORIZED,           /* Unauthorized to access host */
        HRC_PAYMENT_REQUIRED,       /* Payment required */
        HRC_FORBIDDEN,          /* Forbidden to access this URI */
        HRC_NOT_FOUND,          /* URI was not found */
        HRC_METHOD_NOT_ALLOWED,     /* Method is not allowed */
        HRC_NOT_ACCEPTABLE,         /* Not Acceptable */
        HRC_PROXY_AUTHENTICATION,       /* Proxy Authentication is Required */
        HRC_REQUEST_TIMEOUT,            /* Request timed out */
        HRC_CONFLICT,           /* Request is self-conflicting */
        HRC_GONE,               /* Server has gone away */
        HRC_LENGTH_REQUIRED,            /* A content length or encoding is required */
        HRC_PRECONDITION,           /* Precondition failed */
        HRC_REQUEST_TOO_LARGE,      /* Request entity too large */
        HRC_URI_TOO_LONG,           /* URI too long */
        HRC_UNSUPPORTED_MEDIATYPE,      /* The requested media type is unsupported */
        HRC_REQUESTED_RANGE,            /* The requested range is not satisfiable */
        HRC_EXPECTATION_FAILED,     /* The expectation given in an Expect header field was not met */
        HRC_UPGRADE_REQUIRED = 426,     /* Upgrade to SSL/TLS required */
        
        HRC_SERVER_ERROR = 500,     /* Internal server error */
        HRC_NOT_IMPLEMENTED,            /* Feature not implemented */
        HRC_BAD_GATEWAY,            /* Bad gateway */
        HRC_SERVICE_UNAVAILABLE,        /* Service is unavailable */
        HRC_GATEWAY_TIMEOUT,            /* Gateway connection timed out */
        HRC_NOT_SUPPORTED           /* HRC version not supported */
    } http_result_code_t;



    /******* HttpFSM *******/
    #define GET_8(ptr)      (*((uint8_t *)(ptr)))
    #define GET_16(ptr)     (*((uint16_t *)(ptr)))
    #define GET_32(ptr)     (*((uint32_t *)(ptr)))
    #define GET_64(ptr)     (*((uint64_t *)(ptr)))

    class HttpFSM {
    public:
        typedef http_result_code_t result_code;
        enum state { /* http fsm state */
            state_invalid,
            /* recv state */
            state_recv_header,
            state_recv_body,
            state_recv_body_nochunk,
            state_recv_bodylen,
            state_recv_footer,
            state_recv_comment,
            state_recv_finish,
            /* upgrade to websocket */
            state_websocket_establish,
            /* error */
            state_error = -1,
        };
        enum {
            version_1_0 = 10,
            version_1_1 = 11,
        };
        static const uint16_t lflf = 0x0a0a;
        static const uint16_t crlf = 0x0d0a;
        static const uint32_t crlfcrlf = 0x0d0a0d0a;
        static const int MAX_HEADER = 64;
    protected:
        struct context {
            uint8_t     method, version, n_hd, padd;
            int16_t     state, res;
            const char  *hd[MAX_HEADER], *bd;
            uint32_t        bl;
            uint16_t        hl[MAX_HEADER];
        }   m_ctx;
        uint32_t m_max, m_len;
        const char *m_buf;
        char *m_p;
    public:
        HttpFSM() {}
        ~HttpFSM() {}
        state   append(char *b, int bl);
        void    reset(uint32_t chunk_size);
    public:
        void    set_state(state s) { m_ctx.state = s; }
        state   get_state() const { return (state)m_ctx.state; }
        bool    error() const { return get_state() == state_error; }
        void    setrc(result_code rc) { m_ctx.res = (int16_t)rc; }
        void    setrc_from_close_reason(int reason);
    public: /* for processing reply */
        int         version() const { return m_ctx.version; }
        int         hdrlen() const { return m_ctx.n_hd; }
        const char  *hdr(int idx) const { return (idx < hdrlen()) ? m_ctx.hd[idx] : nullptr; }
        char        *hdrstr(const char *key, char *b, int l, int *outlen = NULL) const;
        bool        hashdr(const char *key) {
            char tok[256];
            return hdrstr(key, tok, sizeof(tok)) != NULL;
        }
        bool        hdrint(const char *key, int &out) const;
        bool        accept(const char *mime_type) const {
            return hdr_contains("Accept", mime_type);
        }
        bool        accept_encoding(const char *encoding) const {
            return hdr_contains("Accept-Encoding", encoding);
        }
        bool        hdr_contains(const char *header_name, const char *content) const;
        const char  *body() const { return m_ctx.bd; }
        result_code     rc() const { return (result_code)m_ctx.res; }
        int         bodylen() const { return m_ctx.bl; }
        const char *url(char *b, int l, size_t *p_out = nullptr);
    public: /* util */
        static bool atoi(const char* str, int *i, size_t max);
        static bool htoi(const char* str, int *i, size_t max);
    protected: /* receiving */
        state   recv_header();
        state   recv_body_nochunk();
        state   recv_body();
        state   recv_bodylen();
        state   recv_footer();
        state   recv_comment();
        state   recv_ws_frame();
    protected:
        int     recv_lflf() const;
        int     recv_lf() const;
        char    *current() { return m_p + m_len; }
        const char *current() const { return m_p + m_len; }
        context &recvctx() { return m_ctx; }
        context &sendctx() { return m_ctx; }
        result_code putrc();
    };



    /******* HttpServer *******/
    class HttpServer {
    public:
        struct IResponseWriter {
            virtual void WriteHeader(http_result_code_t, grpc_http_header *, int) = 0;
            virtual void WriteBody(const uint8_t *, size_t) = 0;
            void WriteResponse(const uint8_t *, size_t);
        };
        typedef std::function<void (HttpFSM &, IResponseWriter &)> Callback;
        typedef struct {
            int port;
            Callback callback;
        } ListenerEntry;
    public:
        static HttpServer &Instance();
        HttpServer();
        bool Init();
        bool Listen(int port, Callback cb);
        bool Run(int sleep_ms = 5);
        void Fin();
        //callbacks
        void OnDestroy(grpc_exec_ctx *exec_ctx, grpc_error *error);
        void OnAccept(grpc_exec_ctx *exec_ctx, grpc_endpoint *tcp, grpc_pollset *accepting_pollset,
                      grpc_tcp_server_acceptor *acceptor);
    protected:
        //trampoline
        static void _OnDestroy(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
            ((HttpServer *)arg)->OnDestroy(exec_ctx, error);
        }
        static void _OnAccept(grpc_exec_ctx *exec_ctx, void *server,
                              grpc_endpoint *tcp, grpc_pollset *accepting_pollset,
                              grpc_tcp_server_acceptor *acceptor) {
            ((HttpServer *)server)->OnAccept(exec_ctx, tcp, accepting_pollset, acceptor);
        }
        //handler adding
        void AddHandler(int port, Callback cb);
        bool DoListen(grpc_exec_ctx *exec_ctx, int port);
        
        //control
        bool alive_;
        //handler map
        ListenerEntry *listeners_;
        int n_listeners_, n_listeners_capacity_;
        //io
        gpr_mu *polling_mu_;
        grpc_pollset *pollset_;
        grpc_tcp_server *server_;
        //callbacks
        grpc_closure on_destroy_;
        //instance
        static HttpServer *instance_;
    };



    /******* HttpRouter *******/
    class HttpRouter {
    public:
        typedef HttpServer::Callback Handler;
        typedef HttpFSM Request;
        typedef HttpServer::IResponseWriter Response;
        HttpRouter() : route_() {}
        void Route(const std::string &path_pattern, Handler h) {
            route_[path_pattern] = h;
        }
        void operator () (Request &req, Response &res) {
            char buff[256];
            const char *path = req.url(buff, sizeof(buff));
            for (auto &it : route_) {
                if (it.first == path) {
                    it.second(req, res);
                }
            }
        }
    protected:
        std::map<std::string, Handler> route_;
    };



    /******* HttpClient *******/
    class HttpClient {
    public:
        typedef std::function<void (int, grpc_http_header*, size_t, const char*, size_t)> Callback;
        static void Get(const char *host, const char *path,
                        grpc_http_header *headers, int n_headers,
                        Callback cb, bool ssl = true, uint32_t timeout_msec = 30000);
        static void Post(const char *host, const char *path,
                         grpc_http_header *headers, int n_headers,
                         const char *body, int blen,
                         Callback cb, bool ssl = true, uint32_t timeout_msec = 30000);
    public:
        static void Init(const std::string &root_cert);
        static void Update(int sleep_ms = 5);
        static void Fin();
        static void Start(const std::string &root_cert);
        static void Stop();
    };
}
