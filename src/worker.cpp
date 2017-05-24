#include "worker.h"
#include <functional>

namespace mtk {
    void Worker::Launch() {
        thr_ = std::thread([this](){ this->Run(); });
    }
    Conn *Worker::New() {
        Conn *c = handler_->NewConn(this, handler_);
        c->Step();
        return c;
    }
    void Worker::OnRegister(Conn *c) { 
        ASSERT(std::find(connections_.begin(), connections_.end(), c) == connections_.end());
        connections_.push_back(c); 
    }
    void Worker::OnUnregister(Conn *c) {
        auto it = std::find(connections_.begin(), connections_.end(), c);
        if (it != connections_.end()) {
            connections_.erase(it);
        }
    }
    void Worker::Run() {
        handler_->TlsInit(this);
        New();
        gpr_timespec wait = gpr_time_from_millis(50, GPR_TIMESPAN);
        const int n_process = 3;
        void* tag;  // uniquely identifies a request.
        bool ok;
        while (true) {
            switch (cq_->AsyncNext(&tag, &ok, wait)) {
                case grpc::CompletionQueue::SHUTDOWN:
                    for (int i = 0; i < connections_.size(); i++) {
                        connections_[i]->Destroy();
                    }
                    handler_->TlsFin(this);
                    return;
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
