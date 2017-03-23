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

//#define REFCNT_CHECK 1

namespace mtk {
    class IJob {
    public:
        virtual ~IJob() {}
        virtual void Step() = 0;
        virtual void Destroy() = 0;
    };
    class Conn;
    class Worker;
    class IHandler {
    public:
        virtual grpc::Status Handle(Conn *c, Request &req) = 0;
        virtual mtk_cid_t Login(Conn *c, Request &req) = 0;
        virtual Conn *NewConn(Worker *worker, IHandler *handler) = 0;
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
        ATOMIC_INT refc_;
#if defined(REFCNT_CHECK)
    public:
        static ATOMIC_INT stream_cnt_;
#endif
    public:
        SVStream() : ctx_(), io_(&ctx_), mtx_(), queue_(), tasks_(), is_sending_(false), user_ctx_(nullptr), refc_(0) {
#if defined(REFCNT_CHECK)
            TRACE("SVStream new {} {}", (void *)this, ++stream_cnt_);
#endif
        }
        virtual ~SVStream();
#if defined(REFCNT_CHECK)
        inline void Ref(const char *file, int line) { 
            TRACE("Ref {}: from {}({}): {}", (void *)this, file, line, refc_);
            ++refc_; 
        }
        inline void Unref(const char *file, int line) { 
            TRACE("Unref {}: from {}({}): {}", (void *)this, file, line, refc_);
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
        inline void SetUserCtx(void *ud) { user_ctx_ = ud; }
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
        inline void Finish(IJob *tag) {
            io_.Finish(Status::OK, tag);
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
        static Map cmap_;
        static PendingMap pmap_;
        static Stream default_;
        static ATOMIC_UINT64 login_cid_seed_;
        static std::mutex cmap_mtx_, pmap_mtx_;

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
        ~Conn() { UNREF(stream_); }
        void Step() override;
        void Destroy() override;
        void ConsumeTask(int n_process);
        inline void Close() { 
            stream_->Close();
        }
        inline void InternalClose() {
            status_ = CLOSE;
        }
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
        inline mtk_msgid_t CurrentMsgId() const { return req_.msgid(); }
        static Stream Get(mtk_cid_t uid) {
            cmap_mtx_.lock();
            auto it = cmap_.find(uid);
            if (it != cmap_.end()) {
                cmap_mtx_.unlock();
                return Stream((*it).second->stream_);
            }
            cmap_mtx_.unlock();
            return default_;
        }
        static void Operate(std::function<void(Map &)> op) {
            cmap_mtx_.lock();
            op(cmap_);
            cmap_mtx_.unlock();
        }
    public:
        template <typename... Args> void LogDebug(const char* fmt, const Args&... args) {
#if defined(DEBUG)
            LOG(info, "tag:conn,id:{},a:{},{}", cid_, stream_->RemoteAddress(), logger::Formatter(fmt, args...));
#endif
        }
        template <typename... Args> void LogInfo(const char* fmt, const Args&... args) {
            LOG(info, "tag:conn,id:{},a:{},{}", cid_, stream_->RemoteAddress(), logger::Formatter(fmt, args...));
        }
        template <typename... Args> void LogError(const char* fmt, const Args&... args) {
            LOG(error, "tag:conn,id:{},a:{},{}", cid_, stream_->RemoteAddress(), logger::Formatter(fmt, args...));
        }        
    public: //following no need to use from user code
        void AcceptLogin(SystemPayload::Login &a) {
           if (a.id() == 0) {
                Error *e = new Error();
                e->set_error_code(MTK_ACCEPT_DENY);
                e->set_payload(a.payload());
                Throw(a.msgid(), e);
            } else {
                Register(a.id());
                ASSERT(status_ == WAIT_LOGIN);
                status_ = READ; //change state as if login not pending on case LOGIN of Step()
                Recv(); //start read again
                SystemPayload::Connect sysrep;
                sysrep.set_id(a.id());
                sysrep.set_payload(a.payload());
                SysRep(a.msgid(), sysrep);
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
            pmap_mtx_.unlock();
            SystemPayload::Login a;
            a.set_login_cid(lcid);
            a.set_id(cid);
            a.set_msgid(msgid);
            a.set_payload(data, datalen);
            mc->SysTask(Request::Login, a);
        }
    protected:
        inline void Recv() {
            stream_->Read(req_, this);
        }
        inline void Finish(bool by_task = false) {
            status_ = CLOSE;
            if (by_task) {
                auto sc = new StreamCloser(stream_);
                stream_->Finish(sc);
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
