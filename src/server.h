#pragma once

#include "conn.h"
#include "worker.h"
#include <grpc++/grpc++.h>
#include <mutex>
#include <condition_variable>

namespace mtk {
	class IServer {
	public: //typedefs
		typedef mtk_addr_t Address;
		typedef grpc::SslServerCredentialsOptions CredOptions;
		typedef struct {
			std::string host;
			bool secure;
			CredOptions credential;
		} Listener;
	protected:
		std::thread thread_;
		std::mutex mutex_;
		std::condition_variable cond_;
		std::unique_ptr<grpc::Server> server_;
		std::vector<Worker*> workers_;
	public:
		IServer() : thread_(), mutex_(), server_(), workers_() {}
		virtual ~IServer() {
			for (Worker *w : workers_) {
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
				Shutdown();
				thread_.join();
			}
			delete this;
		}
		uint32_t GetPorts(int port_index, int *ports_buf, uint32_t n_ports_buf);
	public://interface
		virtual void Run() = 0;
		virtual void Shutdown();
	protected:
		void Kick(const Address *addrs, int n_addr, int n_worker, IHandler *h);
		static bool CreateCred(const Address &c, CredOptions &options);
		static bool LoadFile(const char *path, std::string &content);
	};
}
