#pragma once

#include "mtk.h"
#include "stream.h"

namespace mtk {
	class ServerRunner {
	private:
		static ServerRunner *instance_;
	public:
		typedef mtk_svconf_t Config;
		static ServerRunner &Instance();
		void Run(Config &conf, class IHandler *h_read, class IHandler *h_write, DuplexStream::ServerCredOptions *options = nullptr);
	};
}
