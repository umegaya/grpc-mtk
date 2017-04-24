#include "server.h"
#include "worker.h"

namespace mtk {
void IServer::Shutdown() {
    logger::info("ev:sv shutdown start");
    //indicate worker to start shutdown
    for (Worker *w : workers_) {
        w->PrepareShutdown();
    }
    //wait until all RPC processed
    if (server_ != nullptr) {
        server_->Shutdown();
    }
    //then IServer::Kick wakeup and do remaining shutdown
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
    if (server_ == nullptr) {
        logger::fatal("ev:server fail to start");
        // thread stop. need to notify cond value
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.notify_one();
        return;
    }
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
    //shutdown read worker to consume queue
    for (Worker *w : workers_) {
        w->Shutdown();
    }
    for (Worker *w : workers_) {
        //need to wait worker thread shutdown otherwise these worker touches freed memory.
        w->Join();
    }
    logger::info("ev:sv shutdown");
}
}
