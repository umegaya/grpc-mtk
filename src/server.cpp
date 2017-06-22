#include "server.h"
#include "worker.h"

namespace mtk {
Conn::Stream IServer::default_stream_;
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
bool IServer::LoadFile(const char *path, std::string &content) {
    if (path == nullptr) {
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        content = std::string(path); //regard path as raw text
        return true;
    }
    FILE *fp = fopen(path, "r");
    if (fp == nullptr) {
        return false;
    }
    char buffer[st.st_size];
    auto sz = fread(buffer, 1, st.st_size, fp);
    content = std::string(buffer, sz);
    return true;
}
bool IServer::CreateCred(const Address &a, CredOptions &options) {
    std::string cert, ca, key;
    if (!LoadFile(a.cert, cert) || 
        !LoadFile(a.ca, ca) ||
        !LoadFile(a.key, key)) {
        return false;
    }
    options.pem_root_certs = ca;
    options.pem_key_cert_pairs = {
        { .private_key = key, .cert_chain = cert },
    };
    return true;
}
uint32_t IServer::GetPorts(int ports_index, int *ports_buf, uint32_t n_ports_buf) {
    return 0;
}
void IServer::Register(Conn *conn) {
    cmap_mtx_.lock();
    cmap_[conn->Id()] = conn;
    cmap_mtx_.unlock();
}
void IServer::Unregister(Conn *conn) {
    auto cid = conn->Id();
    cmap_mtx_.lock();
    auto it = cmap_.find(cid);
    if (it != cmap_.end()) {
        if (it->second == conn) {
            cmap_.erase(cid);
        }
    }
    cmap_mtx_.unlock();
}
Conn::Stream IServer::GetStream(mtk_cid_t uid) {
    cmap_mtx_.lock();
    auto it = cmap_.find(uid);
    if (it != cmap_.end()) {
        cmap_mtx_.unlock();
        return Conn::Stream((*it).second->stream_);
    }
    cmap_mtx_.unlock();
    return default_stream_;
}
void IServer::ScanConn(std::function<void(Map &)> op) {
    cmap_mtx_.lock();
    op(cmap_);
    cmap_mtx_.unlock();
}
void IServer::Kick(const Address *addrs, int n_addr, int n_worker, IHandler *h) {
    Stream::AsyncService service;
    grpc::ServerBuilder builder;
	// listening port
    for (int i = 0; i < n_addr; i++) {
        const auto &a = addrs[i];
        CredOptions credential;
        bool secure = CreateCred(a, credential);
        if (secure) {
            builder.AddListeningPort(a.host, grpc::SslServerCredentials(credential));
        } else {
            builder.AddListeningPort(a.host, grpc::InsecureServerCredentials());
        }
    }
    // Register service and start sv
    builder.RegisterService(&service);
    // setup worker thread (need to do before BuildAndStart because completion queue should be created before)
    for (int i = 0; i < n_worker; i++) {
        Worker *w = new Worker(&service, this, h, builder);
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
