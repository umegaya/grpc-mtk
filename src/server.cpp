#include "server.h"
#include "worker.h"
#include "conn.h"
#include "http.h"
#include <grpc++/grpc++.h>

namespace mtk {
ServerRunner *ServerRunner::instance_ = nullptr;
ServerRunner &ServerRunner::Instance() {
	if (instance_ == nullptr) {
		instance_ = new ServerRunner();
	}
	return *instance_;
}
void ServerRunner::Run(const std::string &listen_at, const Config &conf, IHandler *rhandler, IHandler *whandler, ServerThread &th, 
                        DuplexStream::ServerCredOptions *options) {
    Stream::AsyncService service;
    grpc::ServerBuilder builder;
	// listening port
    bool secure = false;
    if (options != nullptr) {
        secure = true;
        builder.AddListeningPort(listen_at, grpc::SslServerCredentials(*options));
    } else {
        builder.AddListeningPort(listen_at, grpc::InsecureServerCredentials());
    }
    // Register service and start sv
    builder.RegisterService(&service);
    // setup worker thread (need to do before BuildAndStart because completion queue should be created before)
    std::vector<IWorker*> read_workers;
    for (int i = 0; i < conf.thread.n_reader; i++) {
        IWorker *w = new ReadWorker(&service, rhandler, builder);
        read_workers.push_back(w);
    }
    std::vector<IWorker*> write_workers;
    for (int i = 0; i < conf.thread.n_writer; i++) {
        IWorker *w = new WriteWorker(&service, whandler, builder);
        write_workers.push_back(w);
    }
    
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());

    // start worker thread
    for (IWorker *w : read_workers) {
        w->Launch();
    }
    for (IWorker *w : write_workers) {
        w->Launch();
    }
    
    // notify this thread ready
    th.Signal();
    // Wait for the server to shutdown. 
    server->Wait();
}
}
