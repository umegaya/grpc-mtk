#include "conn.h"
#include "worker.h"

namespace mtk {
    template <>
    bool SVStream::SetupPayload<std::string>(Reply &rep, const std::string &w) {
        rep.set_payload(w.c_str(), w.length());
        return true;
    }
    template <>
    bool SVStream::SetupRequest<std::string>(Request &req, MessageType type, const std::string &w) {
        req.set_type(type);
        req.set_payload(w.c_str(), w.length());
        return true;
    }
    void RSVStream::ConsumeTask(int n_process) {
        Request *t;
        while (n_process != 0 && tasks_.try_dequeue(t)) {
            if (mtk_unlikely(t->kind() != Request::Normal)) {
                switch(t->kind()) {
                case Request::Login: {
                    SystemPayload::Login lreq;
                    if (Codec::Unpack((const uint8_t *)t->payload().c_str(), t->payload().length(), lreq) >= 0) {
                        tag_->AcceptLogin(lreq);
                    } else {
                        ASSERT(false);
                        InternalClose();
                    }
                } break;
                case Request::Close: {
                    step_ = StepId::CLOSE_BY_TASK;
                    Finish();
                } break;
                default:
                    TRACE("invalid system payload kind: {}", t->kind());
                    ASSERT(false);
                    break;
                }
            } else {
                handler_->Handle(tag_, *t);
            }
            n_process--;
        }        
    }
    void RSVStream::Step() {
        //TRACE("RSVStream[{}]:step = {}", (void *)this, step_);
        switch(step_) {
            case StepId::INIT:
                step_ = StepId::ACCEPT;
                //server read stream read from write stream of client
                service_->RequestWrite(&ctx_, &io_, cq_, cq_, tag_);
                break;
            case StepId::ACCEPT:
                handler_->NewConn(worker_, service_, handler_, cq_)->Step(); //create next waiter
                step_ = StepId::LOGIN;
                io_.Read(&req_, tag_);
                break;
            case StepId::LOGIN: {
                //TODO: im not sure why, but after Login processed, one more Step() will be called.
                //so any kind of io_.XXXX should not be called here.
                mtk_cid_t cid = handler_->Login(tag_, req_);
                if (mtk_unlikely(step_ == StepId::CLOSE || worker_->Dying())) {
                    this->LogInfo("ev:app closed by {}", worker_->Dying() ? "Shutdown starts" : "Login failure");
                    step_ = StepId::CLOSE;
                } if (step_ == StepId::WAIT_LOGIN) {
                    //skip processing and wait 
                } else {
                    tag_->Register(cid);
                    this->LogInfo("ev:accept");
                    step_ = StepId::BEFORE_READ;
                }
            } break;
            case StepId::BEFORE_READ: {
                //when write for login reply finished, reach here. prepare for read loop
                io_.Read(&req_, tag_);
                step_ = StepId::READ;
            } break;
            case StepId::READ: {
                if (sender_ == nullptr) {
                    this->LogDebug("ev:handshake not finished,ptr:{}", (void *)tag_);
                    step_ = StepId::CLOSE;
                    Finish();
                    break;
                }
                Status st = handler_->Handle(tag_, req_);
                if (mtk_likely(st.ok() && (step_ != StepId::CLOSE && !worker_->Dying()))) {
                    io_.Read(&req_, tag_);
                } else {
                    this->LogInfo("ev:app closed by {}", st.ok() ? "Close called" : (worker_->Dying() ? "Shutdown starts" : "RPC error"));
                    step_ = StepId::CLOSE;
                    Finish();
                }
            } break;
            case StepId::CLOSE: {
                tag_->Destroy();
            } break;
            case StepId::CLOSE_BY_TASK: {
                //TRACE("CLOSE_BY_TASK: {} {} {}", req_.kind(), req_.type(), req_.msgid());
                step_ = StepId::CLOSE;
            } break;
            case StepId::WAIT_LOGIN: {
                ASSERT(false);
                io_.Read(&req_, tag_); //ignore all received packet
            } break;
            default:
                ASSERT(false);
                this->LogDebug("ev:unknown step,step:{}", step_);
                step_ = StepId::CLOSE;
                Finish();
                break;
        }
    }
    void WSVStream::Step() {
        //TRACE("WSVStream[{}]:step = {}", (void *)this, step_);
        switch(step_) {
            case StepId::INIT:
                step_ = StepId::ACCEPT;
                //server write stream write to read stream of client
                service_->RequestRead(&ctx_, &io_, cq_, cq_, tag_);
                break;
            case StepId::ACCEPT:
                handler_->NewConn(worker_, service_, handler_, cq_)->Step(); //create next waiter
                step_ = StepId::READ;
                io_.Read(&req_, tag_);
                break;
            case StepId::READ: {
                mtk_cid_t cid = handler_->Login(tag_, req_);
                if (mtk_likely(cid != 0 && !worker_->Dying())) {
                    tag_->Register(cid);
                    step_ = StepId::WRITE;
                } else {
                    Finish();
                    step_ = StepId::CLOSE;
                }
            } break;
            case StepId::WRITE: {
                mtx_.lock();
                if (queue_.size() > 0) {
                    //pop one from queue and send it.
                    Reply *r = &(*queue_.front());
                    if (mtk_unlikely(r == nullptr)) {
                        this->LogDebug("ev:mark connection close,tag:{}", (void *)tag_);
                        step_ = StepId::CLOSE;
                        io_.Finish(Status::OK, tag_);
                        break;
                    }
                    io_.Write(*r, tag_);
                    delete r; //todo return r to cache instead of delete
                    queue_.pop();
                } else {
                    //turn off sending flag so that next Send call kick io.Write again
                    //otherwise, WSVStream never processed by this worker thread.
                    is_sending_ = false;
                }
                mtx_.unlock();
                break;
            }
            case StepId::CLOSE: {
                tag_->Destroy();
            } break;
            default:
                this->LogDebug("ev:unknown step,step:{}", step_);
                ASSERT(false);
                break;
        }
    }
    void WSVStream::Terminate() {
        mtx_.lock();
        //indicate destroying WConn
        if (step_ == StepId::WRITE) {
            if (is_sending_) {
                Cleanup(); //no need to send remained packet
                queue_.push(nullptr); //mark close (will handled in Step)
                ASSERT(step_ == StepId::WRITE);
            } else {
                step_ = StepId::CLOSE;
                io_.Finish(Status::OK, tag_);
            }
        }
        mtx_.unlock();
    }
}
