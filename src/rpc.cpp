#include "rpc.h"
#include "codec.h"
#include <grpc++/grpc++.h>
#include <cmath>
#include <chrono>
#include <grpc++/impl/grpc_library.h>

namespace mtk {
extern std::string cl_ca;
extern std::string cl_key;
extern std::string cl_cert;
Reply *IOThread::DISCONNECT_EVENT = reinterpret_cast<Reply*>(0x0);
Reply *IOThread::ESTABLISHED_EVENT = reinterpret_cast<Reply*>(0x1);
Request *IOThread::ESTABLISH_REQUEST = reinterpret_cast<Request*>(0x2);
Reply *RPCStream::DISCONNECT_EVENT = IOThread::DISCONNECT_EVENT;
Reply *RPCStream::ESTABLISHED_EVENT = IOThread::ESTABLISHED_EVENT;
Request *RPCStream::ESTABLISH_REQUEST = IOThread::ESTABLISH_REQUEST;
Error *RPCStream::TIMEOUT_ERROR = nullptr;

/* holding reference to grpc library, to prevent repeated grpc_init/shutdown on the fly */
static grpc::internal::GrpcLibraryInitializer g_gli_initializer;
static GrpcLibraryCodegen s_lib;

/* IOThread */
void IOThread::Initialize(const char *addr, CredOptions *options) {
    g_gli_initializer.summon();
    if (options != nullptr) {
        stub_ = std::unique_ptr<Stub>(new Stream::Stub(grpc::CreateChannel(addr, grpc::SslCredentials(*options))));
    } else {
        stub_ = std::unique_ptr<Stub>(new Stream::Stub(grpc::CreateChannel(addr, grpc::InsecureChannelCredentials())));
    }
}
void IOThread::Finalize() {
    if (context_ != nullptr) {
        delete context_;
    }
    if (prev_io_ != nullptr) {
        delete prev_io_;
    }
}
void IOThread::Start() {
    if (!thr_.joinable()) {
        thr_ = std::thread(std::bind(&IOThread::Run, this));
    };
}
void IOThread::Stop() {
    if (thr_.joinable()) {
        sending_shutdown_ = true;
        if (owner_.IsConnecting() || owner_.IsConnected()) {
            //TRACE("Stop: do graceful shutdown");
            owner_.SendShutdownRequest();
        } else {
            //TRACE("Stop: shutdown immediately");
            cq_.Shutdown();
        }
        thr_.join();
    }
    Finalize();
    delete this;
}
void IOThread::Run() {
    void *tag;
    bool ok;
    gpr_timespec wait = gpr_time_from_millis(50, GPR_TIMESPAN);

    // Block until the next result is available in the completion queue "cq".
    while (alive_) {
        switch (cq_.AsyncNext(&tag, &ok, wait)) {
            case grpc::CompletionQueue::SHUTDOWN:
                alive_ = false;
                break;
            case grpc::CompletionQueue::GOT_EVENT:
                HandleEvent(ok, tag);
                break;
            case grpc::CompletionQueue::TIMEOUT:
                if (!is_sending_) {
                    Request *req;
                    if (owner_.PopRequest(req)) {
                        if (req == ESTABLISH_REQUEST) {
                            Open();
                        } else {
                            io_->Write(*req, GenerateWriteTag(req->msgid()));
                            delete req;
                            is_sending_ = true;
                        }
                    }
                }
                break;
        }
    }
}
void IOThread::HandleEvent(bool ok, void *tag) {
    Request *req;
    if (IsWriteOperation(tag)) {
        if (!ok) {
            //connection closed. stop sending
            return;
        }
        //end of write operation
        if (owner_.PopRequest(req)) {
            io_->Write(*req, GenerateWriteTag(req->msgid()));
            //TRACE("Write:{}", req->msgid());
            delete req;
        } else {
            is_sending_ = false;
        }
        //TRACE("write for tag {}({}) finished", (void *)tag, sidx);
        return;
    } else if (IsReadOperation(tag)) {
        Reply *r = reply_work_.release();
        if (!ok) {
            if (sending_shutdown_) {
                TRACE("connection shutdown. seq {}", connect_sequence_num_);
                cq_.Shutdown();
                return;
            }
            uint32_t seq = ConnectSequenceFromReadTag(tag);
            TRACE("connection closed. seq {} {}", seq, connect_sequence_num_);
            if (r != nullptr) {
                delete r;
            }
            owner_.DrainRequestQueue();
            ASSERT(connect_sequence_num_ >= seq);
            //here, previous connection closed or current connection closed.
            //anyway we can remove previous one.
            if (prev_io_ != nullptr) {
                delete prev_io_;
                prev_io_ = nullptr;
            }
            if (connect_sequence_num_ == seq) {
                //push event pointer to indicate connection closed
                owner_.PushReply(DISCONNECT_EVENT);
            }
        } else {
            if (r != nullptr) {
                //end of read operation. 
                if (!owner_.PushReply(r)) {
                    ASSERT(false);
                }
            } else {
                //call establishment
                if (prev_io_ != nullptr) {
                    delete prev_io_;
                    prev_io_ = nullptr;
                }
                owner_.PushReply(ESTABLISHED_EVENT);
            }
            reply_work_.reset(new Reply());
            io_->Read(reply_work_.get(), GenerateReadTag(connect_sequence_num_));
        }
    } else if (IsCloseOperation(tag)) {
        TRACE("stream close done");
        alive_ = false;
    } else {
        ASSERT(false);
    }
}
void IOThread::SetDeadline(ClientContext &ctx, uint32_t duration_msec) {
    if (duration_msec == UINT32_MAX) {
        ctx.set_deadline(gpr_inf_future(GPR_CLOCK_REALTIME));
        return;
    }
    gpr_timespec ts = GRPCTime(duration_msec);
    ctx.set_deadline(ts);
}

gpr_timespec IOThread::GRPCTime(uint32_t duration_msec) {
    gpr_timespec ts;
    grpc::Timepoint2Timespec(
        std::chrono::system_clock::now() + std::chrono::milliseconds(duration_msec), &ts);
    return ts;
}

/* RPCStream */
int RPCStream::Initialize(const char *addr, CredOptions *options) {
    iothr_->Initialize(addr, options);
    reqmtx_.lock();
    if (TIMEOUT_ERROR == nullptr) {
        auto err = new Error();
        err->set_error_code(MTK_TIMEOUT);
        TIMEOUT_ERROR = err;
    }
    reqmtx_.unlock();
    return 0;
}

void RPCStream::Release() {
    restarting_ = true;
    replys_.enqueue(DISCONNECT_EVENT);
}

void RPCStream::Finalize() {
    iothr_->Stop();
}

timespec_t RPCStream::Tick() {
    return clock::now();
}

timespec_t RPCStream::ReconnectWaitUsec() {
    timespec_t now = Tick();
    if (reconnect_when_ < now) {
        return 0;
    } else {
        return reconnect_when_ - now;
    }
}

timespec_t RPCStream::CalcReconnectWaitDuration(int n_attempt) {
    timespec_t base;
    if (n_attempt <= 1) {
        base = clock::sec(5);
    } else {
        //TODO: use 1.3 ^ n_attempt instead?
        base = clock::sec(std::min(300, (5 << (n_attempt - 2))));
    }
    return CalcJitter(base);
}
timespec_t RPCStream::CalcJitter(timespec_t base) {
    return std::ceil(((double)(800 + rand() % 500) * base) / 1000); //0.800 to 1.200 times
}

void RPCStream::StartWrite() {
    SystemPayload::Connect payload;
    delegate_->AddPayload(payload);
    Call(payload, [this](mtk_result_t r, const char *p, size_t len) {
        if (r >= 0 && delegate_->OnOpenStream(r, p, len)) {
            status_ = NetworkStatus::CONNECT;
        } else {
            replys_.enqueue(DISCONNECT_EVENT);
        }
    });
}

void RPCStream::DrainRequestQueue() {
    Request* drain_req;
    while (requests_.try_dequeue(drain_req)) {
        delete drain_req;
    }    
}

void RPCStream::DrainQueue() {
    //drain queue
    Reply* drain_rep;
    while (replys_.try_dequeue(drain_rep)) {
        if (drain_rep == DISCONNECT_EVENT || drain_rep == ESTABLISHED_EVENT) {
            continue; //also ignore another connection state change events.
        }
        if (drain_rep != nullptr) { delete drain_rep; }//ignore following replys, cause already closed.
    }
    DrainRequestQueue();
    reqmtx_.lock();
    for (auto &p : reqmap_) {
        delete p.second;
    }
    reqmap_.clear();
    reqmtx_.unlock();
}

void RPCStream::ProcessReply() {
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
            notifier_(rep->type(), rep->payload().c_str(), rep->payload().length());
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

void RPCStream::ProcessTimeout(timespec_t now) {
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
        
void RPCStream::Update() {
    if (!delegate_->Ready()) {
        DrainQueue();
        restarting_ = true;
        status_ = NetworkStatus::DISCONNECT;
    } else {
        ProcessReply();
    }
    timespec_t now = Tick();
    switch(status_) {
        case NetworkStatus::DISCONNECT: {
            //due to reconnect_attempt_, sleep for a while
            if (!delegate_->Ready() || reconnect_when_ > now) {
                //TRACE("reconnect wait: {}", ReconnectWaitUsec());
                return; //skip reconnection until time comes
            } else {
                status_ = NetworkStatus::CONNECTING;
                reconnect_attempt_++;
                requests_.enqueue(ESTABLISH_REQUEST);
                iothr_->Start();
            }
        } return;
        case NetworkStatus::CONNECTING: {
        } return;
        case NetworkStatus::ESTABLISHED: {
            status_ = NetworkStatus::INITIALIZING;
            StartWrite();
        } break;
        case NetworkStatus::INITIALIZING: {
            //TODO: detect timeout and back to DISCONNECT.
            break;
        }
        case NetworkStatus::CONNECT: {
            delegate_->Poll();
        } break;
    }
    ProcessTimeout(now);
}

template <> void RPCStream::SetSystemPayloadKind<SystemPayload::Connect>(Request &req) { req.set_kind(Request::Connect); }
template <> void RPCStream::SetSystemPayloadKind<SystemPayload::Ping>(Request &req) { req.set_kind(Request::Ping); }
}