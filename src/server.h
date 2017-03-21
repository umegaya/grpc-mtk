#pragma once

#include "conn.h"
#include "worker.h"
#include <grpc++/grpc++.h>
#include <mutex>
#include <condition_variable>

namespace mtk {
	class IServerThread {
		std::thread thread_;
		std::mutex mutex_;
		std::condition_variable cond_;
		std::unique_ptr<grpc::Server> server_;
		std::vector<IWorker*> workers_;
	public:
		IServerThread() : thread_(), mutex_(), server_(), workers_() {}
		virtual ~IServerThread() {
			for (IWorker *w : workers_) {
				delete w;
			}
		}
		inline void Start() {
	        std::unique_lock<std::mutex> lock(mutex_);
			thread_ = std::thread([this] { Run(); });
			cond_.wait(lock);
		}
		inline void Join() { 
			if (thread_.joinable()) {
				thread_.join();
			}
			delete this;
		}
	public://interface
		typedef grpc::SslServerCredentialsOptions CredOptions;
		virtual void Run() = 0;
		virtual void Shutdown();
	protected:
		void Kick(const std::string &listen_at, int n_handler, IHandler *h, CredOptions *options = nullptr);
	};
}
