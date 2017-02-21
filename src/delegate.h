#pragma once

#include "stream.h"

namespace mtk {
	class StreamDelegate : public DuplexStream, IDuplexStreamDelegate {
	protected:
		mtk_clconf_t clconf_;
	public:
		StreamDelegate(mtk_clconf_t *clconf) : DuplexStream(this), clconf_(*clconf) {}
		uint64_t Id() { return clconf_.id; }
	   	bool Valid() { return clconf_.validate == nullptr ? true : clconf_.validate(); }
	    void Connect(std::function<void(Error *)> finished);
	    void Poll() {}
	};
}
