#include "server.h"

#pragma once

namespace mtk {
ServerRunner &ServerRunner::Instance() {
	if (instance_ == nullptr) {
		instance_ = new ServerRunner();
	}
	return *instance_;
}
void ServerRunner::Run(Config &conf, DuplexStream::CredOptions *options) {
	bool secure = false;
    if (options != nullptr) {
        secure = true;
        builder.AddListeningPort(conf.listen_at_, grpc::SslServerCredentials(*options));
    } else {
        builder.AddListeningPort(conf.listen_at_, grpc::InsecureServerCredentials());
    }
    // Register service and start sv
	Stream::AsyncService service;
    builder.RegisterService(&service);
    // setup worker thread (need to do before BuildAndStart because completion queue should be created before it)
    std::vector<Worker<RConn>*> read_workers;
    for (int i = 0; i < c.thread.n_reader; i++) {
        Worker<RConn> *w = new Worker<RConn>(&service, &handler, builder);
        read_workers.push_back(w);
    }
    std::vector<Worker<WConn>*> write_workers;
    for (int i = 0; i < c.thread.n_writer; i++) {
        Worker<WConn> *w = new Worker<WConn>(&service, &handler, builder);
        write_workers.push_back(w);
    }
    
    std::unique_ptr<Server> server(builder.BuildAndStart());
    //g_logger->info("ev:Server listening start,a:{},r:{},s:{}", c.listen_at_.c_str(), c.role_.c_str(), secure ? "secure" : "insecure");

    // start worker thread
    for (Worker<RConn> *w : read_workers) {
        w->Launch();
    }
    for (Worker<WConn> *w : write_workers) {
        w->Launch();
    }
    
    // start thread for http
    bool http_alive = true;
    auto webthr = std::move(std::thread([&http_alive] {
    	HttpClient::Init();
    	while (http_alive) {
			HttpClient::Update();
    	}
    	HttpClient::Fin();
    }));
    
    // Wait for the server to shutdown. 
    server->Wait();
    http_alive = false;
}
}
