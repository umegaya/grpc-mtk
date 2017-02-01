#pragma once

#include <grpc++/server_builder.h>
#include <thread>
#include "conn.h"

namespace {
    using grpc::ServerCompletionQueue;
    using grpc::ServerBuilder;
    using Service = ::mtk::Stream::AsyncService;
}

namespace mtk {
    class IHandler;
    class IWorker {
    protected:
        std::thread thr_;
        Service* service_;
        IHandler* handler_;
        std::unique_ptr<ServerCompletionQueue> cq_;
    public:
        IWorker(Service *service, IHandler *handler, ServerBuilder &builder) :
            service_(service), handler_(handler), cq_(builder.AddCompletionQueue()) {}
        ~IWorker() { if (thr_.joinable()) { thr_.join(); } }
        void Launch();
        void Run();
        virtual void *New() = 0;
        virtual void Process(bool ok, void *tag) = 0;
    };
    class Worker : public IWorker {
    public:
        Worker(Service *service, IHandler *handler, ServerBuilder &builder) :
            IWorker(service, handler, builder) {}
        virtual void *New() {
            IConn *c = handler_->NewConn(service_, handler_, cq_.get());
            c->Step();
            return c;
        }
        virtual void Process(bool ok, void *tag) {
            IConn *c = static_cast<IConn*>(tag);
            if (!ok) {
                c->Destroy();
            } else {
                c->Step();
            }
        }
    };
}
