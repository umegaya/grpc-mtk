#pragma once

#include <functional>
extern "C" {
#include "src/core/lib/http/parser.h"
#include "src/core/lib/http/httpcli.h"
}

namespace mtk {
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
