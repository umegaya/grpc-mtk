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
        bool dying_;
    public:
        IWorker(Service *service, IHandler *handler, ServerBuilder &builder) :
            service_(service), handler_(handler), cq_(builder.AddCompletionQueue()),
            dying_(false) {}
        virtual ~IWorker() { if (thr_.joinable()) { thr_.join(); } }
        void Launch();
        IConn *New();
        inline void Process(bool ok, void *tag) {
            IConn *c = (IConn *)(((uintptr_t)tag) & ~((uintptr_t)0x1));
            if (!ok) {
                c->Destroy();
            } else if (((uintptr_t)tag) & 0x1) {
                c->WStep();
            } else {
                c->Step();
            }
        }
        inline bool Dying() const { return dying_;  }
        inline void Shutdown() { cq_->Shutdown(); }
        inline void PrepareShutdown() { dying_ = true; }
    public: //interface
        virtual void Run();
        virtual void OnRegister(IConn *) {}
        virtual void OnUnregister(IConn *) {}
        //these are called when login postponed (eg. calling external API)
        virtual void OnWaitLogin(IConn *) {}
        virtual void OnFinishLogin(IConn *) {}
    };
    //worker which does IO and periodically call ConsumeTask for each connection
    class TaskConsumableWorker : public IWorker {
    protected:
        std::vector<IConn *> connections_;
    public:
        TaskConsumableWorker(Service *service, IHandler *handler, ServerBuilder &builder) : 
            IWorker(service, handler, builder), connections_() {}
        void Run() override;
        void OnRegister(IConn *c) override { connections_.push_back(c); }
        void OnUnregister(IConn *) override;
        void OnWaitLogin(IConn *c) override { OnRegister(c); }
        void OnFinishLogin(IConn *c) override { OnUnregister(c); }
    };
    //default definition
    typedef IWorker WriteWorker;
    typedef TaskConsumableWorker ReadWorker;
}
