#pragma once

#include <queue>
#include <mutex>
#include "codec.h"
#include "atomic_compat.h"
#include "mtk.grpc.pb.h"
#include "MoodyCamel/concurrentqueue.h"

namespace {
    using grpc::ServerAsyncReaderWriter;
    using grpc::ServerCompletionQueue;
    using grpc::Status;
    using grpc::ServerContext;
    using Service = ::mtk::Stream::AsyncService;
}

namespace mtk {
    class IWorker;
    class IConn {
    public:
        virtual void Step() = 0;
        virtual void Destroy() = 0;
        virtual void ConsumeTask(int) = 0;
    };
    class IHandler {
    public:
        virtual grpc::Status Handle(IConn *c, Request &req) = 0;
        virtual IConn *NewReader(IWorker *worker, Service *service, IHandler *handler, ServerCompletionQueue *cq) = 0;
        virtual IConn *NewWriter(IWorker *worker, Service *service, IHandler *handler, ServerCompletionQueue *cq) = 0;
    };
    typedef uint64_t ConnectionId;
    typedef uint32_t MessageType;
    class SVStream {
    public:
        enum StepId {
            INIT,
            ACCEPT,
            LOGIN,
            BEFORE_READ,
            READ,
            WRITE,
            CLOSE,
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
        ConnectionId owner_uid_;
    public:
        SVStream(IWorker *worker, Service *service, IHandler *handler, ServerCompletionQueue *cq, IConn *tag) :
            worker_(worker), service_(service), handler_(handler), cq_(cq), ctx_(), io_(&ctx_), req_(),
        step_(StepId::INIT), tag_(tag), owner_uid_(0) {}
        virtual ~SVStream() {}
        grpc::string RemoteAddress() const { return ctx_.peer(); }
        void SetId(ConnectionId uid) { owner_uid_ = uid; }
        ConnectionId Id() const { return owner_uid_; }
        void InternalClose() {
            step_ = StepId::CLOSE;
        }
        IWorker *AssignedWorker() { return worker_; }
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
        bool SetupNotify(Reply &rep, MessageType type, const W &w) {
            SetupPayload(rep, w);
            rep.set_type(type);
            rep.set_msgid(0);
            return true;
        }
        template <class W>
        bool SetupReply(Reply &rep, uint32_t msgid, const W &w) {
            SetupPayload(rep, w);
            rep.set_msgid(msgid);
            return true;
        }
        void SetupThrow(Reply &rep, uint32_t msgid, Error *e) {
            rep.set_msgid(msgid);
            rep.set_allocated_error(e);
        }
    public:
        template <typename... Args> void LogDebug(const char* fmt, const Args&... args) {
            //g_logger->debug("tag:conn,id:{},a:{},{}", Id(), RemoteAddress(), spdlog_ex::Formatter(fmt, args...));
        }
        template <typename... Args> void LogInfo(const char* fmt, const Args&... args) {
            //g_logger->info("tag:conn,id:{},a:{},{}", Id(), RemoteAddress(), spdlog_ex::Formatter(fmt, args...));
        }
        template <typename... Args> void LogError(const char* fmt, const Args&... args) {
            //g_logger->error("tag:conn,id:{},a:{},{}", Id(), RemoteAddress(), spdlog_ex::Formatter(fmt, args...));
        }
    };
    template <> bool SVStream::SetupPayload<std::string>(Reply &rep, const std::string &w);
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
            LogInfo("WSVStream destroy {}({}})", this, Id());
        }
        void ConsumeTask(int) {}
        void Step();

        template <class W> void Notify(MessageType type, const W &w) {
            if (step_ != StepId::CLOSE) {
                Reply *r = new Reply(); //todo: get r from cache
                if (!SetupNotify(*r, type, w)) { return; }
                Send(r);
            }
        }
        template <class W> void Rep(uint32_t msgid, const W &w) {
            if (step_ != StepId::CLOSE) {
                Reply *r = new Reply(); //todo: get r from cache
                if (!SetupReply(*r, msgid, w)) { return; }
                Send(r);
            }
        }
        void Throw(uint32_t msgid, Error *e) {
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
        friend class ServiceImpl;
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
        ConcurrentQueue<Request> tasks_;
    public:
        RSVStream(IWorker *worker, Service *service, IHandler *handler, ServerCompletionQueue *cq, IConn *tag) :
            SVStream(worker, service, handler, cq, tag), sender_(), closed_(0), worker_(worker), tasks_() {}
        virtual ~RSVStream() {
            Cleanup();
            LogInfo("RSVStream destroy {}({}})\n", this, Id());
        }
        void SetStream(std::shared_ptr<WSVStream> &c) { sender_ = c; }
        void Step();
        void ConsumeTask(int n_process) {
            Request *t;
            while (n_process != 0 && tasks_.try_dequeue(t)) {
                handler_->Handle(tag_, *t);
            }
        }
        template <class W> void Rep(uint32_t msgid, const W &w) {
            if (sender_ != nullptr) {
                sender_->Rep(msgid, w);
            } else { //only 1 thread do this. so no need to lock.
                Reply r;
                SetupReply(r, msgid, w);
                Write(r);
            }
        }
        template <class W> void Notify(MessageType type, const W &w) {
            if (sender_ != nullptr) {
                sender_->Notify(type, w);
            }
        }
        void Throw(uint32_t msgid, Error *e) {
            if (sender_ != nullptr) {
                sender_->Throw(msgid, e);
            } else { //only 1 thread do this. so no need to lock.
                Reply r;
                SetupThrow(r, msgid, e);
                Write(r);
            }
        }
        void Finish() {
            io_.Finish(Status::OK, tag_);
        }
        void Close() { closed_.store(1); }
        bool IsClosed() const { return closed_.load() != 0; }
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
    };
    template <class S>
    class Conn : IConn {
    public:
        typedef std::map<ConnectionId, Conn*> Map;
        typedef std::shared_ptr<S> Stream;
    protected:
        Stream stream_;
        static Map cmap_;
        static Stream default_;
        static std::mutex cmap_mtx_;
    public:
        Conn(IWorker *worker, Service* service, IHandler *handler, ServerCompletionQueue* cq) :
            stream_(new S(worker, service, handler, cq, this)) {}
        virtual ~Conn() {}
        virtual void ConsumeTask(int n_process) { stream_->ConsumeTask(n_process); }
        inline ConnectionId Id() { return stream_->Id(); }
        inline std::shared_ptr<S> &GetStream() { return stream_; }
        template <class W> void Rep(uint32_t msgid, const W &w) { stream_->Rep(msgid, w); }
        template <class W> void Notify(MessageType type, const W &w) { stream_->Notify(type, w); }
        void Throw(uint32_t msgid, Error *e) { stream_->Throw(msgid, e); }
        void InternalClose() { stream_->InternalClose(); }
        void Close() { stream_->Close(); }
        bool IsClosed() { return stream_->IsClosed(); }
        void Step() { stream_->Step(); }
        grpc::string RemoteAddress() { return stream_->RemoteAddress(); }
    public:
        static Stream &Get(ConnectionId uid) {
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
    protected:
        void Finish() {
            stream_->Finish();
        }
        void Register(ConnectionId uid) {
            cmap_mtx_.lock();
            cmap_[uid] = this;
            stream_->SetId(uid);
            //LogInfo("ev:register to map");
            AssignedWorker()->OnRegister(this);
            cmap_mtx_.unlock();
        }
        void Unregister() {
            cmap_mtx_.lock();
            auto it = cmap_.find(stream_->Id());
            if (it != cmap_.end()) {
                if (it->second == this) {
                    cmap_.erase(stream_->Id());
                }
            }
            AssignedWorker()->OnUnregister(this);
            //LogInfo("ev:unregister to map");
            cmap_mtx_.unlock();
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
    template<typename T> typename Conn<T>::Map Conn<T>::cmap_;
    template<typename T> typename Conn<T>::Stream Conn<T>::default_;
    template<typename T> std::mutex Conn<T>::cmap_mtx_;
    //default writable connection
    typedef Conn<WSVStream> WConn;
}
