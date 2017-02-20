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
    //worker base
    class IWorker {
    protected:
        std::thread thr_;
        Service* service_;
        IHandler* handler_;
        std::unique_ptr<ServerCompletionQueue> cq_;
    public:
        IWorker(Service *service, IHandler *handler, ServerBuilder &builder) :
            service_(service), handler_(handler), cq_(builder.AddCompletionQueue()),
            connections_() {}
        ~IWorker() { if (thr_.joinable()) { thr_.join(); } }
        void Launch();
        IConn *New();
        inline void Process(bool ok, void *tag);
        virtual void Run();
        virtual void OnRegister(IConn *c) {}
        virtual void OnUnregister(IConn *) {}
    };
    //worker which does IO and periodically call ConsumeTask for each connection
    class TaskConsumableWorker : public IWorker {
    protected:
        std::vector<IConn *> connections_;
    public:
        void Run();
        void OnRegister(IConn *c) { connections_.push_back(c); }
        void OnUnregister(IConn *);
    };
    //inlines
    void IWorker::Process(bool ok, void *tag) {
        IConn *c = static_cast<IConn*>(tag);
        if (!ok) {
            c->Destroy();
        } else {
            c->Step();
        }
    }
    //default definition
    typedef IWorker WriteWorker;
    typedef TaskConsumableWorker ReadWorker;
}
