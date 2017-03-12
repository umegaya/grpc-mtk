#include "server.h"
#include "worker.h"

namespace mtk {
void IServerThread::Shutdown() {
    //indicate worker to start shutdown
    for (IWorker *w : read_workers_) {
        w->PrepareShutdown();
    }
    for (IWorker *w : write_workers_) {
        w->PrepareShutdown();
    }
    //wait until all RPC processed
    server_->Shutdown();
    //shutdown read worker to consume queue
    for (IWorker *w : read_workers_) {
        w->Shutdown();
    }
    for (IWorker *w : write_workers_) {
        w->Shutdown();
    }
}
void IServerThread::Kick(const std::string &listen_at, int n_reader, int n_writer, 
                        IHandler *rhandler, IHandler *whandler, CredOptions *options) {
    Stream::AsyncService service;
    grpc::ServerBuilder builder;
	// listening port
    if (options != nullptr) {
        builder.AddListeningPort(listen_at, grpc::SslServerCredentials(*options));
    } else {
        builder.AddListeningPort(listen_at, grpc::InsecureServerCredentials());
    }
    // Register service and start sv
    builder.RegisterService(&service);
    // setup worker thread (need to do before BuildAndStart because completion queue should be created before)
    for (int i = 0; i < n_reader; i++) {
        IWorker *w = new ReadWorker(&service, rhandler, builder);
        read_workers_.push_back(w);
    }
    /*for (int i = 0; i < n_writer; i++) {
        IWorker *w = new WriteWorker(&service, whandler, builder);
        write_workers_.push_back(w);
    }*/
    // create server
    server_ = std::unique_ptr<grpc::Server>(builder.BuildAndStart());
    // start worker thread
    for (IWorker *w : read_workers_) {
        w->Launch();
    }
    for (IWorker *w : write_workers_) {
        w->Launch();
    }
    // notify this thread ready
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.notify_one();
    }
    // Wait for the server to shutdown. 
    server_->Wait();
}
}
