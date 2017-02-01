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
void ServerRunner::Run(Config &conf, IHandler *handler, DuplexStream::ServerCredOptions *options) {
    Stream::AsyncService service;
    grpc::ServerBuilder builder;
	// listening port
    bool secure = false;
    if (options != nullptr) {
        secure = true;
        builder.AddListeningPort(conf.listen_at, grpc::SslServerCredentials(*options));
    } else {
        builder.AddListeningPort(conf.listen_at, grpc::InsecureServerCredentials());
    }
    // Register service and start sv
    builder.RegisterService(&service);
    // setup worker thread (need to do before BuildAndStart because completion queue should be created before it)
    std::vector<Worker*> read_workers;
    for (int i = 0; i < conf.thread.n_reader; i++) {
        Worker *w = new Worker(&service, handler, builder);
        read_workers.push_back(w);
    }
    std::vector<Worker*> write_workers;
    for (int i = 0; i < conf.thread.n_writer; i++) {
        Worker *w = new Worker(&service, handler, builder);
        write_workers.push_back(w);
    }
    
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    //g_logger->info("ev:Server listening start,a:{},r:{},s:{}", c.listen_at_.c_str(), c.role_.c_str(), secure ? "secure" : "insecure");

    // start worker thread
    for (Worker *w : read_workers) {
        w->Launch();
    }
    for (Worker *w : write_workers) {
        w->Launch();
    }
    
    // start thread for http
    HttpClient::Start(conf.root_cert);
    
    // Wait for the server to shutdown. 
    server->Wait();
    HttpClient::Stop();
}
}
