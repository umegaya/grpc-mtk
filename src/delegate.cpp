#include "delegate.h"

namespace mtk {
	void StreamDelegate::Connect(std::function<void(Error *)> finished) {
		SystemPayload::Connect payload;
	    payload.set_id(Id());
	    payload.set_payload(clconf_.payload, clconf_.payload_len);
	    Call(Request::Connect, payload, [this, finished](mtk_result_t r, const char *p, size_t len) {
	        if (r >= 0) {
	            status_ = NetworkStatus::CONNECT;
	            mtk_closure_call(&clconf_.on_connect, r, p, len);
	            finished(nullptr);
	        } else {
	            replys_.enqueue(DISCONNECT_EVENT);
	            mtk_closure_call(&clconf_.on_connect, r, p, len);
	            Error e;
	            if (Codec::Unpack((uint8_t *)p, len, e) < 0) {
		        	e.set_error_code(MTK_APPLICATION_ERROR);
		        }
		        finished(&e);
	        }
	    }, WRITE);
	}
}
