#include "codec.h"
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

namespace {
	using ::google::protobuf::io::ArrayOutputStream;
	using ::google::protobuf::io::CodedInputStream;
    using ::google::protobuf::io::CodedOutputStream;
}

namespace mtk {
	int Codec::Pack(const Message &m, uint8_t *bytes, int len) {
		ArrayOutputStream as(bytes, len);
		CodedOutputStream s(&as);
	    m.SerializeWithCachedSizes(&s);
	    return static_cast<int>(as.ByteCount());
	}
	int Codec::Unpack(const uint8_t *bytes, int len, Message &m) {
		CodedInputStream s(bytes, len);
		return m.MergePartialFromCodedStream(&s) ? s.CurrentPosition() : -1;
	}
}
