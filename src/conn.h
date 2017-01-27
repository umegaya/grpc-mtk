#pragma once

#include <queue>
#include <mutex>
#include "codec.h"
#include "atomic_compat.h"

namespace {
    using grpc::ServerAsyncReaderWriter;
    using grpc::ServerCompletionQueue;
    using grpc::Status;
    using Service = ::mtk::Stream::AsyncService;
}

namespace mtk {
    class IHandler {
    public:
        virtual void Handle() = 0;
    };
    typedef uint64_t ObjectId;
    typedef uint32_t MessageType;
    class ConnBase {
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
        Service* service_;
        Handler* handler_;
        ServerCompletionQueue* cq_;
        ServerContext ctx_;
        ServerAsyncReaderWriter<Reply, Request> io_;
        Request req_;
        StepId step_;
        void *tag_;
        ObjectId owner_uid_;
    public:
        ConnBase(Service* service, Handler *handler, ServerCompletionQueue* cq, void *tag) :
        service_(service), handler_(handler), cq_(cq), ctx_(), io_(&ctx_), req_(),
        step_(StepId::INIT), tag_(tag), owner_uid_(0) {}
        virtual ~ConnBase() {}
        virtual void Step() {}
        grpc::string RemoteAddress() const { return ctx_.peer(); }
        void SetId(ObjectId uid) { owner_uid_ = uid; }
        ObjectId Id() const { return owner_uid_; }
        void InternalClose() {
            step_ = StepId::CLOSE;
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
            if (CPbCodec::Pack(w, buffer, w.ByteSize()) < 0) {
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
    template <> bool ConnBase::SetupPayload<std::string>(Reply &rep, const std::string &w);
    class WConnBase : public ConnBase {
    private:
        std::mutex mtx_; //TODO: if mtx overhead is matter, need to do some trick with atomic primitives
        //but on recent linux this does not seem big issue any more (http://stackoverflow.com/questions/1277627/overhead-of-pthread-mutexes)
        bool is_sending_;
        std::queue<Reply*> queue_;
    public:
        WConnBase(Service* service, Handler *handler, ServerCompletionQueue* cq, void *tag) :
            ConnBase(service, handler, cq, tag), mtx_(), is_sending_(false), queue_() {}
        virtual ~WConnBase() {
            Cleanup();
            TRACE("WConnBase destroy %p(%llu)\n", this, Id());
        }
        void Step();

        template <class W> void Notify(MessageType type, const W &w) {
            if (step_ != StepId::CLOSE) {
                Reply *r = new Reply(); //todo: get r from cache
                if (!SetupNotify(*r, type, w)) { return; }
                Send(r);
            }
        }
        template <class W> void Reply(uint32_t msgid, const W &w) {
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
        friend class RConnBase;
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
    class RConnBase : public ConnBase {
    private:
        std::shared_ptr<WConnBase> sender_;
        ATOMIC_INT closed_;
    public:
        RConnBase(Service* service, Handler *handler, ServerCompletionQueue* cq, void *tag) :
            ConnBase(service, handler, cq, tag), sender_(), closed_(0) {}
        virtual ~RConnBase() {
            Cleanup();
            TRACE("RConnBase destroy %p(%llu)\n", this, Id());
        }
        void SetSender(std::shared_ptr<WConnBase> &c) { sender_ = c; }
        virtual void Step();
        template <class W> void Reply(uint32_t msgid, const W &w) {
            if (sender_ != nullptr) {
                sender_->Reply(msgid, w);
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
    class TRConn : public RConnBase {
        T cache_;
    public:
        TRConn(Service* service, Handler *handler, ServerCompletionQueue* cq, void *tag) :
            RConnBase(service, handler, cq, tag) {}
        virtual ~TRConn() {}
        T &Cache() { return cache_; }
        const T &Cache() const { return cache_; }
    };
    template <class C>
    class ConnContainer {
    public:
        typedef std::map<ObjectId, ConnContainer*> Map;
        typedef std::shared_ptr<C> Sender;
    protected:
        Sender sender_;
        static Map cmap_;
        static Sender default_;
        static std::mutex cmap_mtx_;
    public:
        ConnContainer(Service* service, Handler *handler, ServerCompletionQueue* cq) :
            sender_(new C(service, handler, cq, this)) {}
        virtual ~ConnContainer() {}
        inline ObjectId Id() { return sender_->Id(); }
        inline std::shared_ptr<C> &GetSender() { return sender_; }
        template <class W> void Reply(uint32_t msgid, const W &w) { sender_->Reply(msgid, w); }
        template <class W> void Notify(MessageType type, const W &w) { sender_->Notify(type, w); }
        void Throw(uint32_t msgid, Error *e) { sender_->Throw(msgid, e); }
        void InternalClose() { sender_->InternalClose(); }
        void Close() { sender_->Close(); }
        bool IsClosed() { return sender_->IsClosed(); }
        void Step() { sender_->Step(); }
        grpc::string RemoteAddress() { return sender_->RemoteAddress(); }
    public:
        static Sender &Get(ObjectId uid) {
            cmap_mtx_.lock();
            auto it = cmap_.find(uid);
            if (it != cmap_.end()) {
                cmap_mtx_.unlock();
                return (*it).second->GetSender();
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
            sender_->Finish();
        }
        void Register(ObjectId uid) {
            cmap_mtx_.lock();
            cmap_[uid] = this;
            sender_->SetId(uid);
            //LogInfo("ev:register to map");
            cmap_mtx_.unlock();
        }
        void Unregister() {
            cmap_mtx_.lock();
            auto it = cmap_.find(sender_->Id());
            if (it != cmap_.end()) {
                if (it->second == this) {
                    cmap_.erase(sender_->Id());
                }
            }
            //LogInfo("ev:unregister to map");
            cmap_mtx_.unlock();
        }
    public:
        template <typename... Args> void LogDebug(const char* fmt, const Args&... args) {
            sender_->LogDebug(fmt, args...);
        }
        template <typename... Args> void LogInfo(const char* fmt, const Args&... args) {
            sender_->LogInfo(fmt, args...);
        }
        template <typename... Args> void LogError(const char* fmt, const Args&... args) {
            sender_->LogError(fmt, args...);
        }
    };
    template<typename T> typename ConnContainer<T>::Map ConnContainer<T>::cmap_;
    template<typename T> typename ConnContainer<T>::Sender ConnContainer<T>::default_;
    template<typename T> std::mutex ConnContainer<T>::cmap_mtx_;
}
