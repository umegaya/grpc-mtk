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
    class IWorker {
    protected:
        std::thread thr_;
        Service* service_;
        IHandler* handler_;
        std::unique_ptr<ServerCompletionQueue> cq_;
        std::vector<IConn *> connections_;
        bool dying_;
    public:
        IWorker(Service *service, IHandler *handler, ServerBuilder &builder) :
            service_(service), handler_(handler), cq_(builder.AddCompletionQueue()),
            connections_(), dying_(false) {}
        virtual ~IWorker() { if (thr_.joinable()) { thr_.join(); } }
        void Launch();
        IConn *New();
        inline void Process(bool ok, void *tag) {
            IConn *c = (IConn *)(tag);
            if (mtk_unlikely(!ok)) {
                c->Destroy();
            } else {
                c->Step();
            }
        }
        inline bool Dying() const { return dying_;  }
        inline void Shutdown() { cq_->Shutdown(); }
        inline void PrepareShutdown() { dying_ = true; }
    public: //interface
        void Run();
        void OnRegister(IConn *c);
        void OnUnregister(IConn *);
        void OnWaitLogin(IConn *c) { OnRegister(c); }
        void OnFinishLogin(IConn *c) { OnUnregister(c); }
        void WaitAccept(ServerContext *ctx, ServerAsyncReaderWriter<Reply, Request> *io, IConn *c) {
           service_->RequestWrite(ctx, io, cq_.get(), cq_.get(), c);
        }
    };
    //default definition
    typedef IWorker Worker;
}
