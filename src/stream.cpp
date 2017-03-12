#include "stream.h"
#include "codec.h"
#include <grpc++/grpc++.h>
#include <cmath>

namespace mtk {
extern std::string cl_ca;
extern std::string cl_key;
extern std::string cl_cert;
Reply *DuplexStream::DISCONNECT_EVENT = reinterpret_cast<Reply*>(0x0);
Reply *DuplexStream::ESTABLISHED_EVENT = reinterpret_cast<Reply*>(0x1);
Request *DuplexStream::ESTABLISH_REQUEST = reinterpret_cast<Request*>(0x2);
Error *DuplexStream::TIMEOUT_ERROR = nullptr;


int DuplexStream::Initialize(const char *addr, CredOptions *options) {
    if (options != nullptr) {
        stub_ = std::unique_ptr<Stub>(new Stream::Stub(grpc::CreateChannel(addr, grpc::SslCredentials(*options))));
    } else {
        stub_ = std::unique_ptr<Stub>(new Stream::Stub(grpc::CreateChannel(addr, grpc::InsecureChannelCredentials())));
    }
    reqmtx_.lock();
    if (TIMEOUT_ERROR == nullptr) {
        auto err = new Error();
        err->set_error_code(MTK_TIMEOUT);
        TIMEOUT_ERROR = err;
    }
    reqmtx_.unlock();
    return 0;
}

void DuplexStream::Release() {
    TRACE("DuplexStream::Release");
    restarting_ = true;
    replys_.enqueue(DISCONNECT_EVENT);
}

void DuplexStream::Finalize() {
    alive_ = false;
    if (thr_.joinable()) {
        thr_.join();
    }
}

timespec_t DuplexStream::Tick() {
    return clock::now();
}

timespec_t DuplexStream::ReconnectWaitUsec() {
    timespec_t now = Tick();
    if (reconnect_when_ < now) {
        return 0;
    } else {
        return reconnect_when_ - now;
    }
}

timespec_t DuplexStream::CalcReconnectWaitDuration(int n_attempt) {
    timespec_t base;
    if (n_attempt <= 1) {
        base = clock::sec(5);
    } else {
        //TODO: use 1.3 ^ n_attempt instead?
        base = clock::sec(std::min(300, (5 << (n_attempt - 2))));
    }
    return CalcJitter(base);
}
timespec_t DuplexStream::CalcJitter(timespec_t base) {
    return std::ceil(((double)(800 + rand() % 500) * base) / 1000); //0.800 to 1.200 times
}

void DuplexStream::StartWrite() {
    SystemPayload::Connect payload;
    delegate_->AddPayload(payload, WRITE);
    Call(payload, [this](mtk_result_t r, const char *p, size_t len) {
        if (r >= 0 && delegate_->OnOpenStream(r, p, len, WRITE)) {
            status_ = NetworkStatus::CONNECT;
        } else {
            replys_.enqueue(DISCONNECT_EVENT);
        }
    }, WRITE);
}
void DuplexStream::StartRead() {
    SystemPayload::Connect payload;
    delegate_->AddPayload(payload, READ);
    Call(payload, [this](mtk_result_t r, const char *p, size_t len) {
        status_ = NetworkStatus::CONNECT;
        if (r < 0 || !delegate_->OnOpenStream(r, p, len, READ)) {
            replys_.enqueue(DISCONNECT_EVENT);
        }
    }, READ);
}

void DuplexStream::DrainQueue() {
    //drain queue
    Reply* drain_rep;
    while (replys_.try_dequeue(drain_rep)) {
        if (drain_rep == DISCONNECT_EVENT || drain_rep == ESTABLISHED_EVENT) {
            continue; //also ignore another connection state change events.
        }
        if (drain_rep != nullptr) { delete drain_rep; }//ignore following replys, cause already closed.
    }
    //for disconnection by GPS lost
    for (int i = 0; i < NUM_STREAM; i++) {
        Request* drain_req;
        while (requests_[i].try_dequeue(drain_req)) {
            delete drain_req;
        }
    }
    reqmtx_.lock();
    for (auto &p : reqmap_) {
        delete p.second;
    }
    reqmap_.clear();
    reqmtx_.unlock();
}

void DuplexStream::Update() {
    if (!delegate_->Valid()) {
        DrainQueue();
        restarting_ = true;
        status_ = NetworkStatus::DISCONNECT;
    } else {
        Reply* rep;
        while (replys_.try_dequeue(rep)) {
            if (rep == DISCONNECT_EVENT || rep == ESTABLISHED_EVENT) {
                DrainQueue();
                //set state disconnected
                if (rep == DISCONNECT_EVENT) {
                    status_ = NetworkStatus::DISCONNECT;
                    //set reconnect wait 5, 10, 20, .... upto 300 sec
                    reconnect_when_ = Tick() + delegate_->OnCloseStream(reconnect_attempt_);
                    TRACE("next reconnect wait: {}", ReconnectWaitUsec());
                } else if (rep == ESTABLISHED_EVENT) {
                    reconnect_attempt_ = 0;
                    reconnect_when_ = 0;
                    restarting_ = false;
                    status_ = NetworkStatus::ESTABLISHED;
                }
                break;
            }
            if (restarting_) {
                TRACE("maybe packet received during restarting ({}/{}}). ignored", rep->msgid(), rep->type());
            } else if (rep->msgid() == 0) {
                auto it = notifymap_.find(rep->type());
                if (it != notifymap_.end()) {
                    (*it).second(rep->type(), rep->payload().c_str(), rep->payload().length());
                } else {
                    TRACE("notify not processed: {}", rep->type());
                    ASSERT(false);
                }
            } else {
                reqmtx_.lock();
                auto it = reqmap_.find(rep->msgid());
                if (it != reqmap_.end()) {
                    SEntry *ent = (*it).second;
                    reqmap_.erase(rep->msgid());
                    reqmtx_.unlock();
                    if (rep->has_error()) {
                        (*ent)(nullptr, rep->mutable_error());
                    } else {
                        (*ent)(rep, nullptr);
                    }
                    delete ent;
                } else {
                    reqmtx_.unlock();
                    TRACE("msgid = {} not found", rep->msgid());
                }
            }
            delete rep;
        }
    }
    timespec_t now = Tick();
    switch(status_) {
        case NetworkStatus::DISCONNECT: {
            //due to reconnect_attempt_, sleep for a while
            if (!delegate_->Valid() || reconnect_when_ > now) {
                //TRACE("reconnect wait: {}", ReconnectWaitUsec());
                return; //skip reconnection until time comes
            } else {
                status_ = NetworkStatus::CONNECTING;
                reconnect_attempt_++;
                requests_[WRITE].enqueue(ESTABLISH_REQUEST);
                if (!thr_.joinable()) {
                    thr_ = std::thread(std::bind(&DuplexStream::Receive, this));
                }
            }
        } return;
        case NetworkStatus::CONNECTING: {
        } return;
        case NetworkStatus::ESTABLISHED: {
            status_ = NetworkStatus::INITIALIZING;
            StartWrite();
        } break;
        case NetworkStatus::REGISTER: {
            status_ = NetworkStatus::INITIALIZING;
            StartRead();
        } break;
        case NetworkStatus::INITIALIZING: {
            //TODO: detect timeout and back to DISCONNECT.
            break;
        }
        case NetworkStatus::CONNECT: {
            delegate_->Poll();
        } break;
    }
    reqmtx_.lock();
    uint32_t erased[reqmap_.size()], n_erased = 0;
    SEntry *entries[reqmap_.size()];
    for (auto &p : reqmap_) {
        if ((p.second->start_at_ + TIMEOUT_DURATION) < now) {
            TRACE("request {} start at {} got timeout ({})", p.first, p.second->start_at_, now);
            entries[n_erased] = p.second;
            erased[n_erased++] = p.first;
        }
    }
    for (uint32_t i = 0; i < n_erased; i++) {
        reqmap_.erase(erased[i]);
    }
    reqmtx_.unlock();
    for (uint32_t i = 0; i < n_erased; i++) {
        (*entries[i])(nullptr, TIMEOUT_ERROR);
        delete entries[i];
    }
}

/*
 * this function should be called from worker thread
 */
void DuplexStream::Receive() {
    void *tag;
    bool ok;
    Request *req;
    gpr_timespec wait = gpr_time_from_millis(50, GPR_TIMESPAN);

    //TODO: make reconnect_when_ assure thread safe.
    //conn_ is always accessed after OpenStream set conn_ correctly, so ok.
    
    // Block until the next result is available in the completion queue "cq".
    while (alive_) {
        switch (cq_.AsyncNext(&tag, &ok, wait)) {
            case grpc::CompletionQueue::SHUTDOWN:
                HandleEvent(false, tag);
                break;
            case grpc::CompletionQueue::GOT_EVENT:
                HandleEvent(ok, tag);
                break;
            case grpc::CompletionQueue::TIMEOUT:
                int sidx = 0;
                for (sidx = 0; sidx < NUM_STREAM; sidx++) {
                    if (!is_sending_[sidx]) {
                        if (requests_[sidx].try_dequeue(req)) {
                            if (req == ESTABLISH_REQUEST) {
                                Open();
                            } else {
                                conn_[sidx]->Write(*req, GenerateWriteTag(req->msgid(), (StreamIndex)sidx));
                                delete req;
                                is_sending_[sidx] = true;
                            }
                        }
                    }
                }
                break;
        }
    }
}
void DuplexStream::HandleEvent(bool ok, void *tag) {
    Reply **replys = reply_work_;
    Request *req;
    int stream_idx = 0;
    for (stream_idx = 0; stream_idx < NUM_STREAM; stream_idx++) {
        if ((StreamIndex)stream_idx == StreamFromReadTag(tag)) {
            break;
        }
    }
    if (stream_idx == NUM_STREAM) {
        if (!ok) {
            //connection closed. stop sending
            return;
        }
        //end of write operation
        StreamIndex sidx = StreamFromWriteTag(tag);
        ASSERT(sidx != NUM_STREAM);
        reqmtx_.lock();
        if (requests_[sidx].try_dequeue(req)) {
            conn_[sidx]->Write(*req, GenerateWriteTag(req->msgid(), sidx));
            //TRACE("Write:{}", req->msgid());
            delete req;
        } else {
            is_sending_[sidx] = false;
        }
        reqmtx_.unlock();
        //TRACE("write for tag {}({}) finished", (void *)tag, sidx);
        return;
    }
    Reply *r = replys[stream_idx];
    if (!ok) {
        uint32_t seq = ConnectSequenceFromReadTag(tag);
        TRACE("connection closed. seq {} {}", seq, connect_sequence_num_);
        if (r != nullptr) {
            delete r;
            replys[stream_idx] = nullptr;
        }
        //drain requests queue
        while (requests_[stream_idx].try_dequeue(req)) {
            if (req != nullptr) { delete req; }//ignore following requests, cause already closed.
        }
        ASSERT(connect_sequence_num_ >= seq);
        //here, previous connection closed or current connection closed.
        //anyway we can remove previous ones.
        if (prev_conn_[stream_idx] != nullptr) {
            delete prev_conn_[stream_idx];
            prev_conn_[stream_idx] = nullptr;
        }
        //connection is regarded as "closed" when WRITE stream closed.
        if (connect_sequence_num_ == seq && stream_idx == WRITE) {
            //push event pointer to indicate connection closed
            replys_.enqueue(DISCONNECT_EVENT);
        }
    } else {
        //end of read operation or establishment
        if (r != nullptr) {
            if (!replys_.enqueue(r)) {
                ASSERT(false);
            }
            replys[stream_idx] = nullptr;
        } else {
            if (prev_conn_[stream_idx] != nullptr) {
                delete prev_conn_[stream_idx];
                prev_conn_[stream_idx] = nullptr;
            }
            if (stream_idx == WRITE) {
                replys_.enqueue(ESTABLISHED_EVENT);
            }
        }
        r = new Reply();
        replys[stream_idx] = r;
        conn_[stream_idx]->Read(r, GenerateReadTag(connect_sequence_num_, (StreamIndex)stream_idx));
    }
}

template <> void DuplexStream::SetSystemPayloadKind<SystemPayload::Connect>(Request &req) { req.set_kind(Request::Connect); }
template <> void DuplexStream::SetSystemPayloadKind<SystemPayload::Ping>(Request &req) { req.set_kind(Request::Ping); }


//2016/08/23 iyatomi: personally I don't like std::chrono and gpr_timespec, these are too complex. but author of grpc++ loves c++11 so much. so I want to limited to use of this only here.
#include <chrono>

void DuplexStream::SetDeadline(ClientContext &ctx, uint32_t duration_msec) {
    if (duration_msec == UINT32_MAX) {
        ctx.set_deadline(gpr_inf_future(GPR_CLOCK_REALTIME));
        return;
    }
    gpr_timespec ts = GRPCTime(duration_msec);
    ctx.set_deadline(ts);
}

gpr_timespec DuplexStream::GRPCTime(uint32_t duration_msec) {
    gpr_timespec ts;
    grpc::Timepoint2Timespec(
        std::chrono::system_clock::now() + std::chrono::milliseconds(duration_msec), &ts);
    return ts;
}
}