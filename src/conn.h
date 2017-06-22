#pragma once

#include "mtk.h"
#include <queue>
#include <mutex>
#include "codec.h"
#include "defs.h"
#include "mtk.grpc.pb.h"
#include <MoodyCamel/concurrentqueue.h>
#include "logger.h"
#include "debug.h"

namespace {
    using grpc::ServerAsyncReaderWriter;
    using grpc::ServerCompletionQueue;
    using grpc::Status;
    using grpc::ServerContext;
    using Service = ::mtk::Stream::AsyncService;
}

//#define REFCNT_CHECK 1

namespace mtk {
    class IJob {
    public:
        virtual ~IJob() {}
        virtual void Step() = 0;
        virtual void Destroy() = 0;
    };
    /* slice to get memory block from host language */
    struct MemSlice {
        void *ptr_;
        mtk_size_t len_;
        MemSlice() : ptr_(nullptr), len_(0) {}
        inline ~MemSlice() {
            if (ptr_ != nullptr) {
                free(ptr_);
            }
        }
        inline void Put(const char *p, mtk_size_t l) {
            ptr_ = malloc(l);
            memcpy(ptr_, p, l);
            len_ = l;
        }
    };
    class Conn;
    class Worker;
    class IServer;
    class IHandler {
    public:
        virtual void TlsInit(Worker *w) {};
        virtual void TlsFin(Worker *w) {};
        virtual grpc::Status Handle(Conn *c, Request &req) = 0;
        virtual mtk_cid_t Login(Conn *c, Request &req, MemSlice &s) = 0;
        virtual void Close(Conn *c) = 0;
        virtual Conn *NewConn(Worker *worker, IHandler *handler) = 0;
        virtual void Poll() {}
    };
    typedef uint32_t MessageType;
    class SVStream : public IJob {
    public:
        typedef moodycamel::ConcurrentQueue<Request*> TaskQueue;
    protected:
        friend class Conn;
        ServerContext ctx_;
        ServerAsyncReaderWriter<Reply, Request> io_;
        std::mutex mtx_;
        std::queue<Reply*> queue_;
        moodycamel::ConcurrentQueue<Request*> tasks_;
        bool is_sending_;
        void *user_ctx_;
        void (*user_ctx_dtor_)(void *);
        ATOMIC_INT refc_;
#if defined(REFCNT_CHECK)
    public:
        static ATOMIC_INT stream_cnt_;
#endif
    public:
        SVStream() : ctx_(), io_(&ctx_), mtx_(), queue_(), tasks_(), is_sending_(false), user_ctx_(nullptr), user_ctx_dtor_(nullptr), refc_(0) {
#if defined(REFCNT_CHECK)
            TRACE("SVStream new {} {}", (void *)this, ++stream_cnt_);
#endif
        }
        virtual ~SVStream();
#if defined(REFCNT_CHECK)
        inline void Ref(const char *file, int line) { 
            TRACE("Ref {}: from {}({}): {}", (void *)this, file, line, refc_.load());
            ++refc_; 
        }
        inline void Unref(const char *file, int line) { 
            TRACE("Unref {}: from {}({}): {}", (void *)this, file, line, refc_.load());
            if (--refc_ <= 0) { delete this; } 
        } 
        #define REF(s) { s->Ref(__FILE__, __LINE__); }
        #define UNREF(s) { s->Unref(__FILE__, __LINE__); }
#else
        inline void Ref() { ++refc_; }
        inline void Unref() { if (--refc_ <= 0) { delete this; } } 
        #define REF(s) { s->Ref(); }
        #define UNREF(s) { s->Unref(); }
#endif
        //implements IJob
        void Step() override;
        void Destroy() override { UNREF(this); }
    public:
        inline grpc::string RemoteAddress() const { return ctx_.peer(); }
        inline TaskQueue &Tasks() { return tasks_; }
        template <class CTX> inline CTX &UserCtx() { return *(CTX *)user_ctx_; }
        template <class CTX> inline const CTX &UserCtx() const { return *(const CTX *)user_ctx_; }
        inline void *UserCtxPtr() { return user_ctx_; }
        inline void SetUserCtx(void *ud, void (*dtor)(void *) = nullptr) { 
            user_ctx_ = ud; 
            user_ctx_dtor_ = dtor;
        }
        template <class W> 
        inline void Rep(mtk_msgid_t msgid, const W &w) {
            Reply *r = new Reply();
            SetupReply(*r, msgid, w);
            Send(r);
        }
        template <class W> 
        inline void SysRep(mtk_msgid_t msgid, const W &w) {
            Reply *r = new Reply();
            SetupSystemReply(*r, msgid, w);
            Send(r);
        }
        template <class W> 
        inline void Notify(MessageType type, const W &w) {
            Reply *r = new Reply(); //todo: get r from cache
            SetupNotify(*r, type, w);
            Send(r);                
        }
        inline void Throw(mtk_msgid_t msgid, Error *e) {
            Reply *r = new Reply(); //todo: get r from cache
            SetupThrow(*r, msgid, e);   
            Send(r);
        }
        void Send(Reply *r) {
            mtx_.lock();
            //TRACE("Send {} {} {} {} {} {} {}", (void *)this, (void *)r, queue_.size(), r->type(), r->msgid(), r->has_error(), is_sending_);
            if (!is_sending_) {
                is_sending_ = true;
                REF(this);
                Write(*r, this);
                delete r;
            } else {
                queue_.push(r);
            }
            mtx_.unlock();
        }
        size_t QueueSize() {
            size_t sz;
            mtx_.lock();
            sz = queue_.size();
            mtx_.unlock();
            return sz;
        }
        template <class W> inline void AddTask(MessageType type, const W &w) {
            Request *r = new Request(); //todo: get r from cache
            SetupRequest(*r, type, w);
            tasks_.enqueue(r);
        }
        template <class W> inline void SysTask(Request::Kind type, const W &w) {
            Request *r = new Request(); //todo: get r from cache
            SetupSysTask(*r, type, w);
            tasks_.enqueue(r);
        }
        inline void Close() { 
            SystemPayload::Close c;
            SysTask(Request::Close, c);
        }
    protected:
        inline void DestroyUserCtx() {
            if (user_ctx_ != nullptr) {
                if (user_ctx_dtor_ != nullptr) {
                    user_ctx_dtor_(user_ctx_);
                }
            }
        }
        inline void Finish(IJob *tag) {
            io_.Finish(Status::CANCELLED, tag);
        }
        template <class R>
        inline void Read(R &r, IJob *tag) {
            io_.Read(&r, tag);
        }
        template <class W> 
        inline void Write(const W &w, IJob *tag) {
            io_.Write(w, tag);
        }
        template <class W>
        static inline bool SetupPayload(Reply &rep, const W &w) {
            uint8_t buffer[w.ByteSize()];
            if (Codec::Pack(w, buffer, w.ByteSize()) < 0) {
                ASSERT(false);
                return false;
            }
            rep.set_payload(buffer, w.ByteSize());
            return true;
        }
        template <class W>
        static inline bool SetupRequest(Request &req, MessageType type, const W &w) {
            uint8_t buffer[w.ByteSize()];
            if (Codec::Pack(w, buffer, w.ByteSize()) < 0) {
                ASSERT(false);
                return false;
            }
            req.set_type(type);
            req.set_payload(buffer, w.ByteSize());
            return true;
        }
        template <class W>
        static inline bool SetupSysTask(Request &req, Request::Kind k, const W &w) {
            uint8_t buffer[w.ByteSize()];
            if (Codec::Pack(w, buffer, w.ByteSize()) < 0) {
                ASSERT(false);
                return false;
            }
            req.set_kind(k);
            req.set_payload(buffer, w.ByteSize());
            return true;
        }
        template <class W>
        static inline bool SetupNotify(Reply &rep, MessageType type, const W &w) {
            SetupPayload(rep, w);
            rep.set_type(type);
            rep.set_msgid(0);
            return true;
        }
        template <class W>
        static inline bool SetupReply(Reply &rep, mtk_msgid_t msgid, const W &w) {
            SetupPayload(rep, w);
            rep.set_msgid(msgid);
            return true;
        }
        template <class W>
        static inline bool SetupSystemReply(Reply &rep, mtk_msgid_t msgid, const W &w) {
            uint8_t buffer[w.ByteSize()];
            if (Codec::Pack(w, buffer, w.ByteSize()) < 0) {
                ASSERT(false);
                return false;
            }
            rep.set_msgid(msgid);
            rep.set_payload(buffer, w.ByteSize());
            return true;
        }
        static inline void SetupThrow(Reply &rep, mtk_msgid_t msgid, Error *e) {
            rep.set_msgid(msgid);
            rep.set_allocated_error(e);
        }
    };
    template <> bool SVStream::SetupPayload<std::string>(Reply &rep, const std::string &w);
    template <> bool SVStream::SetupRequest<std::string>(Request &req, MessageType type, const std::string &w);
    class StreamCloser : public IJob {
    protected:
        SVStream *stream_;
    public:
        StreamCloser(SVStream *s) : stream_(s) { REF(stream_); }
        ~StreamCloser() { UNREF(stream_); }
        void Step() override { Destroy(); }
        void Destroy() override { delete this; }
    };
    class Conn : public IJob {
        friend class IServer;
    public:
        typedef std::map<mtk_cid_t, Conn*> Map;
        typedef std::map<mtk_login_cid_t, Conn*> PendingMap;
        enum StreamStatus {
            INIT,
            ACCEPT,
            LOGIN,
            WAIT_LOGIN,
            READ,
            WRITE,
            CLOSE,
        };
        class Stream {
            SVStream *stream_;
        public:
            Stream(SVStream *s) : stream_(s) { REF(stream_); }
            Stream() : stream_(nullptr) {}
            ~Stream() { if (stream_ != nullptr) { UNREF(stream_); } }
            SVStream *operator -> () { return stream_; }
            const SVStream *operator -> () const { return stream_; }
            SVStream &operator * () { return *stream_; }
            const SVStream &operator * () const { return *stream_; }
            bool operator == (std::nullptr_t) const { return stream_ == nullptr; }
            bool operator != (std::nullptr_t) const { return stream_ != nullptr; }
        };
    protected:
        static PendingMap pmap_;
        static ATOMIC_UINT64 login_cid_seed_;
        static std::mutex pmap_mtx_;

        Worker *worker_;
        IHandler *handler_;
        SVStream *stream_;
        StreamStatus status_;
        Request req_;
        mtk_cid_t cid_, lcid_;
    public:
        Conn(Worker *worker, IHandler *handler) :
            worker_(worker), handler_(handler), stream_(new SVStream()), 
            status_(INIT), req_(), cid_(0), lcid_(0) { REF(stream_); }
        ~Conn() { 
            UNREF(stream_); 
            stream_ = (SVStream *)0xdeadbeef;
        }
        void Step() override;
        void Destroy() override;
        void ConsumeTask(int n_process);
        inline void Close() { 
            stream_->Close();
        }
        inline void InternalClose() {
            status_ = CLOSE;
        }
        inline void *UserCtxPtr() { return stream_->UserCtxPtr(); }
        inline void SetUserCtx(void *ud, void (*dtor)(void *) = nullptr) { stream_->SetUserCtx(ud, dtor); }
        template <class W> inline void Rep(mtk_msgid_t msgid, const W &w) {
            stream_->Rep(msgid, w); 
        }
        template <class W> inline void SysRep(mtk_msgid_t msgid, const W &w) {
            stream_->SysRep(msgid, w); 
        }
        template <class W> inline void Notify(MessageType type, const W &w) {
            stream_->Notify(type, w); 
        }
        inline void Throw(mtk_msgid_t msgid, Error *e) {
            stream_->Throw(msgid, e);
        }    
        template <class W> inline void AddTask(MessageType type, const W &w) {
            stream_->AddTask(type, w);
        }
        template <class W> inline void SysTask(Request::Kind type, const W &w) {
            stream_->SysTask(type, w);
        }
    public:
        inline mtk_cid_t Id() const { return cid_; }
        inline bool WaitLoginAccept() const { return status_ == WAIT_LOGIN; }
        inline bool HasPeer() const { return status_ > ACCEPT; }
        inline mtk_msgid_t CurrentMsgId() const { return req_.msgid(); }
        static Stream Get(IServer *sv, mtk_cid_t uid);
        static void Operate(IServer *sv, std::function<void(Map &)> op);
    public:
        template <typename... Args> void LogDebug(const char* fmt, const Args&... args) {
#if defined(DEBUG)
            LOG(info, "tag:conn,id:{},a:{},{}", cid_, stream_->RemoteAddress(), logger::Format(fmt, args...));
#endif
        }
        template <typename... Args> void LogInfo(const char* fmt, const Args&... args) {
            LOG(info, "tag:conn,id:{},a:{},{}", cid_, stream_->RemoteAddress(), logger::Format(fmt, args...));
        }
        template <typename... Args> void LogError(const char* fmt, const Args&... args) {
            LOG(error, "tag:conn,id:{},a:{},{}", cid_, stream_->RemoteAddress(), logger::Format(fmt, args...));
        }        
    public: //following no need to use from user code
        mtk_cid_t Login() {
            MemSlice s;
            mtk_cid_t cid = handler_->Login(this, req_, s);
            if (WaitLoginAccept()) {
                return cid;
            } else if (cid != 0) {
                SystemPayload::Connect sysrep;
                sysrep.set_id(cid);
                if (s.len_ > 0) {
                    ASSERT(s.ptr_ != nullptr);
                    sysrep.set_payload(s.ptr_, s.len_);
                }
                SysRep(req_.msgid(), sysrep);
            } else {
                Error *e = new Error();
                e->set_error_code(MTK_ACCEPT_DENY);
                if (s.len_ > 0) {
                    ASSERT(s.ptr_ != nullptr);
                    e->set_payload(s.ptr_, s.len_);
                }
                Throw(req_.msgid(), e);
            }
            return cid;

        }
        bool AcceptLogin(SystemPayload::Login &a) {
           if (a.id() == 0) {
                Error *e = new Error();
                e->set_error_code(MTK_ACCEPT_DENY);
                e->set_payload(a.payload());
                Throw(a.msgid(), e);
                return false;
            } else {
                Register(a.id());
                ASSERT(status_ == WAIT_LOGIN);
                status_ = READ; //change state as if login not pending on case LOGIN of Step()
                Recv(); //start read again
                SystemPayload::Connect sysrep;
                sysrep.set_id(a.id());
                sysrep.set_payload(a.payload());
                SysRep(a.msgid(), sysrep);
                return true;
            }
        }
        static mtk_login_cid_t DeferLogin(mtk_svconn_t c) {
            auto mc = (Conn*)c;
            mc->status_ = WAIT_LOGIN;
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
            pmap_.erase(lcid);
            pmap_mtx_.unlock();
            SystemPayload::Login a;
            a.set_login_cid(lcid);
            a.set_id(cid);
            a.set_msgid(msgid);
            a.set_payload(data, datalen);
            mc->SysTask(Request::Login, a);
        }
        static Conn *FindDeferred(mtk_login_cid_t lcid) {
            pmap_mtx_.lock();
            auto it = pmap_.find(lcid);
            if (it == pmap_.end()) {
                pmap_mtx_.unlock();
                return nullptr;
            }
            auto mc = it->second;
            pmap_mtx_.unlock();
            return mc;
        }
    protected:
        inline void Recv() {
            stream_->Read(req_, this);
        }
        inline void Finish(bool by_task = false) {
            StreamStatus prev_st = status_;
            status_ = CLOSE;
            if (by_task) {
                auto sc = new StreamCloser(stream_);
                stream_->Finish(sc);
                if (prev_st == WAIT_LOGIN) {
                    //because in wait_login status, this Conn should not register to completion queue, 
                    //so never Destroy()'ed by Conn::Step()
                    TRACE("removed as its in wait login mode {}", (void *)this);
                    Destroy();
                }
            } else {
                stream_->Finish(this);
            }
        }
        void Register(mtk_cid_t cid);
        void Unregister();
        mtk_login_cid_t NewLoginCid();
        void ClearLoginCid();
    };
}
