#pragma once

#include "mtk.h"
#include <queue>
#include <mutex>
#include "codec.h"
#include "defs.h"
#include "mtk.grpc.pb.h"
#include <MoodyCamel/concurrentqueue.h>
#include "debug.h"

namespace {
    using grpc::ServerAsyncReaderWriter;
    using grpc::ServerCompletionQueue;
    using grpc::Status;
    using grpc::ServerContext;
    using Service = ::mtk::Stream::AsyncService;
}

namespace mtk {
    class IWorker;
    //TODO: separate IReadConn, IWriteConn, because some of interfaces are not necessary for write handler.
    class IConn {
    public:
        virtual ~IConn() {}
        virtual void Step() = 0;
        virtual void WStep() = 0;
        virtual void Destroy() = 0;
        virtual void Register(mtk_cid_t cid) = 0;
        virtual bool WaitLoginAccept() = 0;
        virtual void AcceptLogin(SystemPayload::Login &) = 0;
        virtual void ConsumeTask(int) = 0;
    };
    //TODO: separate IReadHandler, IWriteHandler, because some of interfaces are not necessary for write handler.
    class IHandler {
    public:
        virtual grpc::Status Handle(IConn *c, Request &req) = 0;
        virtual mtk_cid_t Login(IConn *c, Request &req) = 0;
        virtual IConn *NewConn(IWorker *worker, Service *service, IHandler *handler, ServerCompletionQueue *cq) = 0;
    };
    typedef uint32_t MessageType;
    class SVStream {
    public:
        enum StepId {
            INIT,
            ACCEPT,
            LOGIN,
            WAIT_LOGIN,
            READ,
            WRITE,
            CLOSE,
            CLOSE_BY_TASK,
        };
    protected:
        IWorker *worker_;
        Service *service_;
        IHandler *handler_;
        ServerCompletionQueue *cq_;
        ServerContext ctx_;
        ServerAsyncReaderWriter<Reply, Request> io_;
        Request req_;
        StepId step_;
        IConn *tag_;
        mtk_cid_t owner_uid_;
    public:
        SVStream(IWorker *worker, Service *service, IHandler *handler, ServerCompletionQueue *cq, IConn *tag) :
            worker_(worker), service_(service), handler_(handler), cq_(cq), ctx_(), io_(&ctx_), req_(),
        step_(StepId::INIT), tag_(tag), owner_uid_(0) {}
        virtual ~SVStream() {}
        virtual void OnClose() {}
        inline grpc::string RemoteAddress() const { return ctx_.peer(); }
        inline void SetId(mtk_cid_t uid) { owner_uid_ = uid; }
        inline mtk_cid_t Id() const { return owner_uid_; }
        inline uint32_t CurrentMsgId() const { return req_.msgid(); }
        inline IWorker *AssignedWorker() { return worker_; }
        inline void InternalClose() {
            step_ = StepId::CLOSE;
        }
        inline void WaitLogin() {
            step_ = StepId::WAIT_LOGIN;
        }
        inline bool WaitLoginAccept() { 
            return step_ == StepId::WAIT_LOGIN;
        }
        inline void Accepted() {
            step_ = StepId::READ;
        }
        inline void Finish() {
            io_.Finish(Status::OK, tag_);
        }
    protected:
        template <class W> void Write(const W &w) {
            if (step_ != StepId::CLOSE) {
                io_.Write(w, tag_);
            }
        }
        template <class W>
        bool SetupPayload(Reply &rep, const W &w) {
            uint8_t buffer[w.ByteSize()];
            if (Codec::Pack(w, buffer, w.ByteSize()) < 0) {
                return false;
            }
            rep.set_payload(buffer, w.ByteSize());
            return true;
        }
        template <class W>
        bool SetupRequest(Request &req, MessageType type, const W &w) {
            uint8_t buffer[w.ByteSize()];
            if (Codec::Pack(w, buffer, w.ByteSize()) < 0) {
                return false;
            }
            req.set_type(type);
            req.set_payload(buffer, w.ByteSize());
            return true;
        }
        template <class W>
        bool SetupSysTask(Request &req, Request::Kind k, const W &w) {
            uint8_t buffer[w.ByteSize()];
            if (Codec::Pack(w, buffer, w.ByteSize()) < 0) {
                return false;
            }
            req.set_kind(k);
            req.set_payload(buffer, w.ByteSize());
            return true;
        }
        template <class W>
        bool SetupNotify(Reply &rep, MessageType type, const W &w) {
            SetupPayload(rep, w);
            rep.set_type(type);
            rep.set_msgid(0);
            return true;
        }
        template <class W>
        bool SetupReply(Reply &rep, mtk_msgid_t msgid, const W &w) {
            SetupPayload(rep, w);
            rep.set_msgid(msgid);
            return true;
        }
        template <class W>
        bool SetupSystemReply(Reply &rep, mtk_msgid_t msgid, const W &w) {
            uint8_t buffer[w.ByteSize()];
            if (Codec::Pack(w, buffer, w.ByteSize()) < 0) {
                return false;
            }
            rep.set_msgid(msgid);
            rep.set_payload(buffer, w.ByteSize());
            return true;
        }
        void SetupThrow(Reply &rep, mtk_msgid_t msgid, Error *e) {
            rep.set_msgid(msgid);
            rep.set_allocated_error(e);
        }
    public:
        template <typename... Args> void LogDebug(const char* fmt, const Args&... args) {
#if defined(DEBUG)
            LOG(info, "tag:conn,id:{},a:{},{}", Id(), RemoteAddress(), logger::Formatter(fmt, args...));
#endif
        }
        template <typename... Args> void LogInfo(const char* fmt, const Args&... args) {
            LOG(info, "tag:conn,id:{},a:{},{}", Id(), RemoteAddress(), logger::Formatter(fmt, args...));
        }
        template <typename... Args> void LogError(const char* fmt, const Args&... args) {
            LOG(error, "tag:conn,id:{},a:{},{}", Id(), RemoteAddress(), logger::Formatter(fmt, args...));
        }
    };
    template <> bool SVStream::SetupPayload<std::string>(Reply &rep, const std::string &w);
    template <> bool SVStream::SetupRequest<std::string>(Request &req, MessageType type, const std::string &w);
    class WSVStream : public SVStream {
    private:
        std::mutex mtx_; //TODO: if mtx overhead is matter, need to do some trick with atomic primitives
        //but on recent linux this does not seem big issue any more (http://stackoverflow.com/questions/1277627/overhead-of-pthread-mutexes)
        bool is_sending_;
        std::queue<Reply*> queue_;
    public:
        WSVStream(IWorker *worker, Service *service, IHandler *handler, ServerCompletionQueue *cq, IConn *tag) :
            SVStream(worker, service, handler, cq, tag), mtx_(), is_sending_(false), queue_() {}
        virtual ~WSVStream() {
            Cleanup();
            LogInfo("WSVStream destroy {}({})", (void *)this, Id());
        }
        void ConsumeTask(int) {}
        void Step();
        void WStep() { Step(); }

        template <class W> void Notify(MessageType type, const W &w) {
            if (step_ != StepId::CLOSE) {
                Reply *r = new Reply(); //todo: get r from cache
                if (!SetupNotify(*r, type, w)) { return; }
                Send(r);
            }
        }
        template <class W> void Rep(mtk_msgid_t msgid, const W &w) {
            if (step_ != StepId::CLOSE) {
                Reply *r = new Reply(); //todo: get r from cache
                if (!SetupReply(*r, msgid, w)) { return; }
                Send(r);
            }
        }
        template <class W> void SysRep(mtk_msgid_t msgid, const W &w) {
            if (step_ != StepId::CLOSE) {
                Reply *r = new Reply(); //todo: get r from cache
                if (!SetupSystemReply(*r, msgid, w)) { return; }
                Send(r);
            }
        }
        void Throw(mtk_msgid_t msgid, Error *e) {
            if (step_ != StepId::CLOSE) {
                Reply *r = new Reply(); //todo: get r from cache
                SetupThrow(*r, msgid, e);
                Send(r);
            }
        }
        void Send(Reply *r) {
            mtx_.lock();
            if (!is_sending_) {
                is_sending_ = true;
                io_.Write(*r, tag_);
                delete r;
            } else {
                queue_.push(r);
            }
            mtx_.unlock();
        }
    protected:
        friend class RSVStream;
        void Terminate();
        void Cleanup() {
            while (queue_.size() > 0) {
                //drain queue
                Reply *r = &(*queue_.front());
                if (r != nullptr) { delete r; }
                queue_.pop();
            }
        }
    };
    class RSVStream : public SVStream {
    private:
        std::shared_ptr<WSVStream> sender_;
        ATOMIC_INT closed_;
        IWorker *worker_;
        moodycamel::ConcurrentQueue<Request*> tasks_;
        std::mutex mtx_; //TODO: if mtx overhead is matter, need to do some trick with atomic primitives
        //but on recent linux this does not seem big issue any more (http://stackoverflow.com/questions/1277627/overhead-of-pthread-mutexes)
        std::queue<Reply*> queue_;
        bool is_sending_; int8_t ref_;
    public:
        RSVStream(IWorker *worker, Service *service, IHandler *handler, ServerCompletionQueue *cq, IConn *tag) :
            SVStream(worker, service, handler, cq, tag), sender_(), closed_(0), worker_(worker), tasks_(), 
            mtx_(), queue_(), is_sending_(false), ref_(1) {}
        virtual ~RSVStream() {
            Cleanup();
            LogInfo("RSVStream destroy {}({})", (void *)this, Id());
        }
        inline void SetStream(std::shared_ptr<WSVStream> &c) { sender_ = c; }
        void Step();
        void WStep();
        void ConsumeTask(int n_process);
        template <class W> void Rep(mtk_msgid_t msgid, const W &w) {
            if (sender_ != nullptr) {
                sender_->Rep(msgid, w);
            } else { //only 1 thread do this. so no need to lock.
                Reply *r = new Reply();
                SetupReply(*r, msgid, w);
                Send(r);
            }
        }
        template <class W> void SysRep(mtk_msgid_t msgid, const W &w) {
            if (sender_ != nullptr) {
                sender_->SysRep(msgid, w);
            } else { //only 1 thread do this. so no need to lock.
                Reply *r = new Reply();
                SetupSystemReply(*r, msgid, w);
                Send(r);
            }
        }
        template <class W> void Notify(MessageType type, const W &w) {
            if (sender_ != nullptr) {
                sender_->Notify(type, w);
            } else {
                Reply *r = new Reply(); //todo: get r from cache
                if (!SetupNotify(*r, type, w)) { return; }
                Send(r);                
            }
        }
        template <class W> void AddTask(MessageType type, const W &w) {
            Request *r = new Request(); //todo: get r from cache
            SetupRequest(*r, type, w);
            tasks_.enqueue(r);
        }
        template <class W> void SysTask(Request::Kind type, const W &w) {
            Request *r = new Request(); //todo: get r from cache
            SetupSysTask(*r, type, w);
            tasks_.enqueue(r);
        }
        void Throw(mtk_msgid_t msgid, Error *e) {
            if (sender_ != nullptr) {
                sender_->Throw(msgid, e);
            } else { //only 1 thread do this. so no need to lock.
                Reply *r = new Reply(); //todo: get r from cache
                SetupThrow(*r, msgid, e);
                Write(r);
            }
        }
        void Read() {
            ref_++;
            io_.Read(&req_, tag_);
        }
        void Write(Reply *r) {
            ref_++;
            io_.Write(*r, (void *)(((uintptr_t)tag_) | 0x1));            
        }
        inline void Finish() {
            ref_++;
            io_.Finish(Status::OK, tag_);
        }
        void Send(Reply *r) {
            mtx_.lock();
            if (!is_sending_) {
                is_sending_ = true;
                Write(r);
                delete r;
            } else {
                queue_.push(r);
            }
            mtx_.unlock();
        }
        void Close() { closed_.store(1); }
        bool IsClosed() const { return closed_.load() != 0; }
        void OnClose() override {
            ConsumeTask(-1); //consume all task
        }
    protected:
        virtual void Cleanup() {
            if (sender_ != nullptr) {
                sender_->Terminate();
            }
        }
    };
    template <class T>
    class TRSVStream : public RSVStream {
        T context_;
    public:
        TRSVStream(IWorker *worker, Service *service, IHandler *handler, ServerCompletionQueue *cq, IConn *tag) :
            RSVStream(worker, service, handler, cq, tag) {}
        virtual ~TRSVStream() {}
        T &Context() { return context_; }
        const T &Context() const { return context_; }
        void OnClose() override {
            RSVStream::OnClose();
            context_.Destroy();
        }
    };
    template <class S>
    class Conn : public IConn {
    public:
        typedef std::shared_ptr<S> Stream;
    protected:
        Stream stream_;
    public:
        Conn(IWorker *worker, Service* service, IHandler *handler, ServerCompletionQueue* cq) :
            stream_(std::make_shared<S>(worker, service, handler, cq, this)) {}
        virtual ~Conn() {}
        //implements IConn
        void ConsumeTask(int n_process) override { stream_->ConsumeTask(n_process); }
        void Step() override { stream_->Step(); }
        void WStep() override { stream_->WStep(); }
        bool WaitLoginAccept() override { return stream_->WaitLoginAccept(); }
        void AcceptLogin(SystemPayload::Login &) override {}
        void Destroy() override { 
            Unregister(); 
            stream_->OnClose();
            //TRACE("{} should delete", (void *)this);
            delete this; 
        }
        //inline 
        inline mtk_cid_t Id() { return stream_->Id(); }
        inline uint32_t CurrentMsgId() const { return stream_->CurrentMsgId(); }
        inline std::shared_ptr<S> &GetStream() { return stream_; }
        inline void Throw(mtk_msgid_t msgid, Error *e) { stream_->Throw(msgid, e); }
        inline void InternalClose() { stream_->InternalClose(); }
        inline void Close() { stream_->Close(); }
        inline bool IsClosed() { return stream_->IsClosed(); }
        inline grpc::string RemoteAddress() { return stream_->RemoteAddress(); }
        template <class W> void Rep(mtk_msgid_t msgid, const W &w) { stream_->Rep(msgid, w); }
        template <class W> void Notify(MessageType type, const W &w) { stream_->Notify(type, w); }
        template <class W> void AddTask(MessageType type, const W &w) { stream_->AddTask(type, w); }
        template <class SPL> void SysRep(mtk_msgid_t msgid, const SPL &spl) { stream_->SysRep(msgid, spl); }
        template <class SPL> void SysTask(Request::Kind type, const SPL &spl) { stream_->SysTask(type, spl); }
    public:
        void Register(mtk_cid_t cid) override {
            stream_->SetId(cid);
            stream_->AssignedWorker()->OnRegister(this);
        }
        virtual void Unregister() {
            stream_->AssignedWorker()->OnUnregister(this);
        }        
    protected:
        inline void Finish() {
            stream_->Finish();
        }
    public:
        template <typename... Args> void LogDebug(const char* fmt, const Args&... args) {
            stream_->LogDebug(fmt, args...);
        }
        template <typename... Args> void LogInfo(const char* fmt, const Args&... args) {
            stream_->LogInfo(fmt, args...);
        }
        template <typename... Args> void LogError(const char* fmt, const Args&... args) {
            stream_->LogError(fmt, args...);
        }
    };
    template <class S> 
    class MappedConn : public Conn<S> {
    public:
        typedef Conn<S> Super;
        typedef typename Super::Stream Stream;
        typedef std::map<mtk_cid_t, MappedConn<S>*> Map;
        typedef std::map<mtk_login_cid_t, MappedConn<S>*> PendingMap;
    protected:
        static Map cmap_;
        static PendingMap pmap_;
        static Stream default_;
        static ATOMIC_UINT64 login_cid_seed_;
        static std::mutex cmap_mtx_, pmap_mtx_;

        mtk_login_cid_t lcid_;
    public:
        MappedConn(IWorker *worker, Service* service, IHandler *handler, ServerCompletionQueue* cq) :
            Super(worker, service, handler, cq), lcid_(0) {}
        static Stream &Get(mtk_cid_t uid) {
            cmap_mtx_.lock();
            auto it = cmap_.find(uid);
            if (it != cmap_.end()) {
                cmap_mtx_.unlock();
                return (*it).second->GetStream();
            }
            cmap_mtx_.unlock();
            return default_;
        }
        static void Operate(std::function<void(Map &)> op) {
            cmap_mtx_.lock();
            op(cmap_);
            cmap_mtx_.unlock();
        }
        void Register(mtk_cid_t cid) override {
            ClearLoginCid();
            cmap_mtx_.lock();
            cmap_[cid] = this;
            cmap_mtx_.unlock();
            Super::Register(cid);
        }
        void Unregister() override {
            ClearLoginCid();
            cmap_mtx_.lock();
            auto it = cmap_.find(Super::stream_->Id());
            if (it != cmap_.end()) {
                if (it->second == this) {
                    cmap_.erase(Super::stream_->Id());
                }
            }
            cmap_mtx_.unlock();
            Super::Unregister();
        }
        void AcceptLogin(SystemPayload::Login &a) override {
           if (a.id() == 0) {
                Error *e = new Error();
                e->set_error_code(MTK_ACCEPT_DENY);
                e->set_payload(a.payload());
                Super::Throw(a.msgid(), e);
            } else {
                Register(a.id());
                Super::GetStream()->Accepted(); //change state
                Super::GetStream()->Read(); //start read again
                SystemPayload::Connect sysrep;
                sysrep.set_id(a.id());
                sysrep.set_payload(a.payload());
                Super::SysRep(a.msgid(), sysrep);
            }
        }
        template <class WC>
        static bool MakePair(mtk_msgid_t cid, WC &wc) {
            typename Super::Stream s = Get(cid);
            if (s == nullptr) {
                return false;
            }
            s->SetStream(wc.GetStream());
            return true;
        }
        static mtk_login_cid_t DeferLogin(mtk_svconn_t c) {
            auto mc = (MappedConn<S>*)c;
            mc->GetStream()->WaitLogin();
            return mc->NewLoginCid();
        }
        static void FinishLogin(mtk_login_cid_t lcid, mtk_cid_t cid, mtk_msgid_t msgid, const char *data, mtk_size_t datalen) {
            pmap_mtx_.lock();
            auto it = pmap_.find(lcid);
            if (it == pmap_.end()) {
                pmap_mtx_.unlock();
                return;
            }
            auto mc = it->second;
            pmap_mtx_.unlock();
            SystemPayload::Login a;
            a.set_login_cid(lcid);
            a.set_id(cid);
            a.set_msgid(msgid);
            a.set_payload(data, datalen);
            mc->SysTask(Request::Login, a);
        }
    protected:
        mtk_login_cid_t NewLoginCid() {
            lcid_ = ++login_cid_seed_;
            pmap_mtx_.lock();
            pmap_[lcid_] = this;
            pmap_mtx_.unlock();
            Super::stream_->AssignedWorker()->OnWaitLogin(this);
            return lcid_;
        }
        void ClearLoginCid() {
            if (lcid_ != 0) {
                pmap_mtx_.lock();
                auto it = pmap_.find(lcid_);
                if (it != pmap_.end()) {
                    pmap_.erase(it);
                }
                pmap_mtx_.unlock();
                lcid_ = 0;
                Super::stream_->AssignedWorker()->OnFinishLogin(this);
            }
        }
    };
    template<typename T> typename MappedConn<T>::Map MappedConn<T>::cmap_;
    template<typename T> std::mutex MappedConn<T>::cmap_mtx_;
    template<typename T> typename MappedConn<T>::PendingMap MappedConn<T>::pmap_;
    template<typename T> std::mutex MappedConn<T>::pmap_mtx_;
    template<typename T> ATOMIC_UINT64 MappedConn<T>::login_cid_seed_;
    template<typename T> typename Conn<T>::Stream MappedConn<T>::default_;
    //default io connection
    typedef Conn<WSVStream> WConn;
    typedef MappedConn<RSVStream> RConn;
}
