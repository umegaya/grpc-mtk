#pragma once

#include "stream.h"

namespace mtk {
	class StreamDelegate : public IDuplexStreamDelegate {
	public:
		typedef bool (*ValidCallback)();
	protected:
		mtk_clconf_t clconf_;
		DuplexStream *stream_;
	public:
		friend class DuplexStream;
		StreamDelegate(mtk_clconf_t *clconf) : clconf_(*clconf) {}
		void SetStream(DuplexStream *s) { stream_ = s; }
	   	bool Valid() { return clconf_.valid_cb(); }
	    void Connect(std::function<void(Error *)> finished);
	    void Poll() {}
	}
}
