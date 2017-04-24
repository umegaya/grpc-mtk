#pragma once

#include <grpc++/server_builder.h>
#include <thread>
#include "conn.h"
#include "defs.h"

namespace {
    using grpc::ServerCompletionQueue;
    using grpc::ServerBuilder;
    using Service = ::mtk::Stream::AsyncService;
}

namespace mtk {
    class IHandler;
    //worker base
    class Worker {
    protected:
        std::thread thr_;
        Service* service_;
        IHandler* handler_;
        std::unique_ptr<ServerCompletionQueue> cq_;
        std::vector<Conn *> connections_;
        bool dying_;
    public:
        Worker(Service *service, IHandler *handler, ServerBuilder &builder) :
            service_(service), handler_(handler), cq_(builder.AddCompletionQueue()),
            connections_(), dying_(false) {}
        virtual ~Worker() {}
        void Launch();
        Conn *New();
        inline void Process(bool ok, void *tag) {
            IJob *c = (IJob *)(tag);
            if (mtk_unlikely(!ok)) {
                c->Destroy();
            } else {
                c->Step();
            }
        }
        inline bool Dying() const { return dying_;  }
        inline void Shutdown() { cq_->Shutdown(); }
        inline void Join() { if (thr_.joinable()) { thr_.join(); } }
        inline void PrepareShutdown() { dying_ = true; }
    public: //interface
        void Run();
        void OnRegister(Conn *c);
        void OnUnregister(Conn *);
        void OnWaitLogin(Conn *c) { OnRegister(c); }
        void OnFinishLogin(Conn *c) { OnUnregister(c); }
        void WaitAccept(ServerContext *ctx, ServerAsyncReaderWriter<Reply, Request> *io, Conn *c) {
           service_->RequestWrite(ctx, io, cq_.get(), cq_.get(), c);
        }
    };
}
