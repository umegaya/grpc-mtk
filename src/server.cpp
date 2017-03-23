#include "server.h"
#include "worker.h"

namespace mtk {
void IServer::Shutdown() {
    //indicate worker to start shutdown
    for (Worker *w : workers_) {
        w->PrepareShutdown();
    }
    //wait until all RPC processed
    server_->Shutdown();
    //shutdown read worker to consume queue
    for (Worker *w : workers_) {
        w->Shutdown();
    }
}
void IServer::Kick(const std::string &listen_at, int n_handler, IHandler *h, CredOptions *options) {
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
    for (int i = 0; i < n_handler; i++) {
        Worker *w = new Worker(&service, h, builder);
        workers_.push_back(w);
    }
    // create server
    server_ = std::unique_ptr<grpc::Server>(builder.BuildAndStart());
    // start worker thread
    for (Worker *w : workers_) {
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
