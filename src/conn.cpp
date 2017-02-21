#include "conn.h"
#if !defined(ASSERT)
#if defined(DEBUG)
#define ASSERT(...) assert(__VA_ARGS__)
#else
#define ASSERT(...)
#endif
#endif

namespace mtk {
    template <>
    bool SVStream::SetupPayload<std::string>(Reply &rep, const std::string &w) {
        rep.set_payload(w.c_str(), w.length());
        return true;
    }
    void RSVStream::Step() {
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
                Status st = handler_->Handle(tag_, req_);
                if (step_ == StepId::CLOSE) {
                    this->LogError("ev:app closed by Login failure");
                    Finish();
                } else if (owner_uid_ != 0) {
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
                    this->LogDebug("ev:handshake not finished,ptr:{}", tag_);
                    step_ = StepId::CLOSE;
                    Finish();
                    break;
                }
                Status st = handler_->Handle((IConn *)tag_, req_);
                if (st.ok() && step_ != StepId::CLOSE) {
                    io_.Read(&req_, tag_);
                } else {
                    this->LogInfo("ev:app closed by {}", st.ok() ? "Close called" : "RPC error");
                    Finish();
                }
            } break;
            case StepId::CLOSE: {
                ((IConn *)tag_)->Destroy();
            } break;
            default:
                this->LogDebug("ev:unknown step,step:{}", step_);
                step_ = StepId::CLOSE;
                Finish();
                break;
        }
    }
    void WSVStream::Step() {
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
                Status st = handler_->Handle(tag_, req_);
                if (st.ok()) {
                    step_ = StepId::WRITE;
                }
            } break;
            case StepId::WRITE: {
                mtx_.lock();
                if (queue_.size() > 0) {
                    //pop one from queue and send it.
                    Reply *r = &(*queue_.front());
                    if (r == nullptr) {
                        this->LogDebug("ev:mark connection close,tag:{}", tag_);
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
                ((IConn *)tag_)->Destroy();
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
