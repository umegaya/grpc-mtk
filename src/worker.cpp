#include "worker.h"
#include <functional>

namespace mtk {
    void IWorker::Launch() {
        thr_ = std::move(std::thread(std::bind(&IWorker::Run, this)));
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
}
