#include "worker.h"
#include <functional>

namespace mtk {
    void IWorker::Launch() {
        thr_ = std::move(std::thread([this](){ this->Run(); }));
    }
    IConn *IWorker::New() {
        IConn *c = handler_->NewConn(this, service_, handler_, cq_.get());
        c->Step();
        return c;
    }
    void IWorker::Run() {
        New();
        void* tag;  // uniquely identifies a request.
        bool ok;
        while (true) {
            GPR_ASSERT(cq_->Next(&tag, &ok));
            Process(ok, tag);
        }
    }
    //task consumable worker
    void TaskConsumableWorker::OnUnregister(IConn *c) {
        auto it = std::find(connections_.begin(), connections_.end(), c);
        if (it != connections_.end()) {
            connections_.erase(it);
        }
    }
    void TaskConsumableWorker::Run() {
        New();
        gpr_timespec wait = gpr_time_from_millis(50, GPR_TIMESPAN);
        const int n_process = 3;
        void* tag;  // uniquely identifies a request.
        bool ok;
        while (true) {
            switch (cq_->AsyncNext(&tag, &ok, wait)) {
                case grpc::CompletionQueue::SHUTDOWN:
                    Process(false, tag);
                    break;
                case grpc::CompletionQueue::GOT_EVENT:
                    Process(ok, tag);
                    break;
                case grpc::CompletionQueue::TIMEOUT:
                    break;
            }
            for (int i = 0; i < connections_.size(); i++) {
                auto c = connections_[i];
                c->ConsumeTask(n_process);
            }
        }
    }
}
