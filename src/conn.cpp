#include "conn.h"
#include "worker.h"

namespace mtk {
    //SVStream
#if defined(REFCNT_CHECK)
    ATOMIC_INT SVStream::stream_cnt_(0);
#endif
    SVStream::~SVStream() {
        Request *t;
        while (Tasks().try_dequeue(t)) {
            delete t;            
        }
        DestroyUserCtx();
#if defined(REFCNT_CHECK)
        TRACE("SVStream delete {} {}", (void *)this, --stream_cnt_);
#endif
    }
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
    void SVStream::Step() {
        mtx_.lock();
        //TRACE("RSVStream[{}]:wstep = {}, {}, {}", (void *)this, step_, queue_.size(), is_sending_);
        if (queue_.size() > 0) {
            //pop one from queue and send it.
            Reply *r = &(*queue_.front());
            Write(*r, this);
            delete r; //todo return r to cache instead of delete
            queue_.pop();
            mtx_.unlock();
        } else {
            //turn off sending flag so that next Send call kick io.Write again
            //otherwise, WSVStream never processed by this worker thread.
            is_sending_ = false;
            mtx_.unlock();
            UNREF(this);
        }
    }
    //Conn
    Conn::Map Conn::cmap_;
    Conn::PendingMap Conn::pmap_;
    Conn::Stream Conn::default_;
    ATOMIC_UINT64 Conn::login_cid_seed_;
    std::mutex Conn::cmap_mtx_, Conn::pmap_mtx_;
    void Conn::Destroy() {
        TRACE("ev:conn destroy,ptr:{},has_peer:{}", (void *)this, HasPeer());
        if (HasPeer()) {
            ConsumeTask(-1); //process all task
            handler_->Close(this);
            Unregister();
        }
        delete this;
    }
    void Conn::ConsumeTask(int n_process) {
        Request *t;
        if (mtk_unlikely(worker_->Dying()) && status_ != CLOSE) {
            LogInfo("ev:app closed by Worker shutdown,st:{}", status_);
            Finish(true);
            return;
        }
        while (n_process != 0 && stream_->Tasks().try_dequeue(t)) {
            if (mtk_unlikely(t->kind() != Request::Normal)) {
                switch(t->kind()) {
                case Request::Login: {
                    SystemPayload::Login lreq;
                    //LogInfo("ev:deferred login reply,blen:{}",t->payload().length());
                    if (Codec::Unpack((const uint8_t *)t->payload().c_str(), t->payload().length(), lreq) >= 0) {
                        if (!AcceptLogin(lreq)) {
                            ASSERT(status_ == WAIT_LOGIN);
                            LogInfo("ev:app closed by Deferred Login failure");
                            Finish(true);
                            delete t;
                            return; //this object died. break immediately
                        }
                    } else {
                        ASSERT(false);
                        LogInfo("ev:app closed by Deferred Login invalid payload");
                        Finish(true);
                    }
                } break;
                case Request::Close: {
                    LogInfo("ev:app closed by Server operation");
                    Finish(true);
                } break;
                default:
                    TRACE("invalid system payload kind: {}", t->kind())
                    ASSERT(false);
                    Finish(true);
                    break;
                }
            } else {
                handler_->Handle(this, *t);
            }
            n_process--;
            delete t;
        }        
    }
    void Conn::Step() {
        //TRACE("RSVStream[{}]:step = {}", (void *)this, status_);
        switch(status_) {
            case INIT:
                status_ = ACCEPT;
                //this wait next incoming rpc
                worker_->WaitAccept(&(stream_->ctx_), &(stream_->io_), this);
                break;
            case ACCEPT:
                worker_->New(); //create next waiter, will be wait next incoming rpc
                status_ = LOGIN;
                Recv();
                break;
            case LOGIN: {
                if (mtk_unlikely(req_.kind() == Request::Close)) {
                    LogInfo("ev:app closed by Client shutdown");
                    Finish();
                    break;
                }
                mtk_cid_t cid = handler_->Login(this, req_);
                if (mtk_unlikely(status_ == CLOSE || worker_->Dying())) {
                    LogInfo("ev:app closed by {}", worker_->Dying() ? "Shutdown starts" : "Login failure");
                    Finish();
                } else if (status_ == WAIT_LOGIN) {
                    //skip processing and wait 
                } else {
                    Register(cid);
                    LogInfo("ev:accept");
                    status_ = READ;
                    Recv();
                }
            } break;
            case READ: {
                if (mtk_unlikely(req_.kind() == Request::Close)) {
                    LogInfo("ev:app closed by Client shutdown");
                    Finish();
                    break;
                }
                Status st = handler_->Handle(this, req_);
                if (mtk_likely(st.ok() && (status_ != CLOSE && !worker_->Dying()))) {
                    Recv();
                } else {
                    LogInfo("ev:app closed by {}", st.ok() ? "Close called" : (worker_->Dying() ? "Shutdown starts" : "RPC error"));
                    Finish();
                }
            } break;
            case CLOSE: {
                //TRACE("CLOSE: {} {} {}", (void *)this, req_.type(), req_.msgid());
                Destroy();
            } break;
            case WAIT_LOGIN: {
                ASSERT(false);
                LogError("ev:in wait login state, conn should not read from client");
                Finish();
            } break;
            default:
                ASSERT(false);
                LogError("ev:unknown step,step:{}", status_);
                Finish();
                break;
        }
    }
    void Conn::Register(mtk_cid_t cid) {
        ClearLoginCid();
        cmap_mtx_.lock();
        cmap_[cid] = this;
        cmap_mtx_.unlock();
        cid_ = cid;
        worker_->OnRegister(this);
    }
    void Conn::Unregister() {
        ClearLoginCid();
        cmap_mtx_.lock();
        auto it = cmap_.find(cid_);
        if (it != cmap_.end()) {
            if (it->second == this) {
                cmap_.erase(cid_);
            }
        }
        cmap_mtx_.unlock();
        worker_->OnUnregister(this);
    }
    mtk_login_cid_t Conn::NewLoginCid() {
        lcid_ = ++login_cid_seed_;
        pmap_mtx_.lock();
        pmap_[lcid_] = this;
        pmap_mtx_.unlock();
        worker_->OnWaitLogin(this);
        return lcid_;
    }
    void Conn::ClearLoginCid() {
        if (lcid_ != 0) {
            pmap_mtx_.lock();
            auto it = pmap_.find(lcid_);
            if (it != pmap_.end()) {
                pmap_.erase(it);
            }
            pmap_mtx_.unlock();
            lcid_ = 0;
            worker_->OnFinishLogin(this);
        }
    }    
}
