#include "delegate.h"

namespace mtk {
	void StreamDelegate::Connect(std::function<void(Error *)> finished) {
		SystemPayload::Connect payload;
	    payload.set_id(delegate_->Id());
	    payload.set_payload(clconf_.connect_payload, clconf_.connect_payload_len)
	    Call(Request::Kind::Connect, payload, [stream_, &clconf_](Reply *rep, Error *err) {
	        if (rep != nullptr) {
	            stream_->status_ = NetworkStatus::CONNECT;
	            clconf_.login_cb(0, rep->payload().c_str(), rep->payload().length());
	        } else {
	            stream_->replys_.enqueue(DISCONNECT_EVENT);
	            clconf_.login_cb(err->error_code(), err->payload().c_str(), err->payload().length());
	        }
	    });
	}
}
