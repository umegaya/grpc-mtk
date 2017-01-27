#pragma once

#include <google/protobuf/message.h>

namespace mtk {
	class Codec {
	public:
	    typedef ::google::protobuf::Message Message;
	    static int Pack(const Message &m, uint8_t *bytes, int len);
	    static int Unpack(const uint8_t *bytes, int len, Message &m);
	};
}
