#pragma once

#include "mtk.h"
#include "stream.h"
#include <mutex>
#include <condition_variable>

namespace mtk {
	class ServerThread {
		std::thread thread_;
		std::mutex mutex_;
		std::unique_lock<std::mutex> lock_;
		std::condition_variable cond_;
		mtk_queue_t queue_;
	public:
		ServerThread(bool exclusive) : mutex_(), lock_(mutex_), queue_(nullptr) {
			if (exclusive) {
				lock_.unlock();
			}
		}
		~ServerThread() {
			if (queue_ != nullptr) {
				mtk_queue_destroy(queue_);
			}
			mutex_.unlock();
		}
		void Assign(std::thread &&t) {
			thread_ = std::move(t);
		}
		void Assign(mtk_queue_t q) {
			queue_ = q;
		}
		mtk_queue_t Queue() { return queue_; }
		void Join() { 
			if (thread_.joinable()) {
				thread_.join();
			}
			delete this;
		}
		void Signal() {
			std::unique_lock<std::mutex> lock(mutex_);
			cond_.notify_one();
		}
		void Wait() {
			cond_.wait(lock_);
			mutex_.unlock();
		}
	};
	class ServerRunner {
	private:
		static ServerRunner *instance_;
	public:
		typedef mtk_svconf_t Config;
		typedef mtk_addr_t Address;
		static ServerRunner &Instance();
		void Run(const std::string &listen_at, const Config &conf, class IHandler *h_read, class IHandler *h_write, ServerThread &th, 
				DuplexStream::ServerCredOptions *options = nullptr);
		void Run(const Address &listen_at, const Config &conf, ServerThread &th);
	};
}
