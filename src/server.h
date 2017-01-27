#pragma once

#include "mtk.h"
#include "stream.h"

namespace mtk {
	class ServerRunner {
	private:
		ServerRunner *instance_;
	public:
		typedef mtk_svconf_t Config;
		static ServerRunner &Instance();
		void Run(Config &conf, DuplexStream::CredOptions *options = nullptr);
	};
}
