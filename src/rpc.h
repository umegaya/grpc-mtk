#pragma once

#include "mtk.h"
#include "proto/src/mtk.grpc.pb.h"
#include "codec.h"
#include "timespec.h"
#include <MoodyCamel/concurrentqueue.h>
#include <thread>
#include <mutex>
#include <functional>
#include "defs.h"
#include "debug.h"

namespace {
    using grpc::Channel;
    using grpc::ClientAsyncReaderWriter;
    using grpc::ClientContext;
    using grpc::CompletionQueue;
    using grpc::Status;
    using moodycamel::ConcurrentQueue;
}

namespace grpc {
    class SslCredentialsOptions;
    class SslServerCredentialsOptions;
}

namespace mtk {
    class RPCStream;
    class IOThread {
        friend class RPCStream;
    protected:
        typedef Stream::Stub Stub;
        typedef grpc::SslCredentialsOptions CredOptions;
        static Reply *DISCONNECT_EVENT;
        static Reply *ESTABLISHED_EVENT;
        static Request *ESTABLISH_REQUEST;
    protected:
        RPCStream &owner_;
        std::thread thr_;
        std::unique_ptr<Stub> stub_;
        std::unique_ptr<ClientAsyncReaderWriter<Request, Reply>> io_;
        CompletionQueue cq_;
        ClientAsyncReaderWriter<Request, Reply> *prev_io_;
        ClientContext *context_;
        Reply *reply_work_;
        uint32_t connect_sequence_num_;
        bool is_sending_, alive_;
    public:
        IOThread(RPCStream &s) : owner_(s), thr_(), stub_(), io_(), cq_(), 
            prev_io_(nullptr), context_(nullptr), reply_work_(nullptr), 
            connect_sequence_num_(0), is_sending_(false), alive_(true) {}
        void Initialize(const char *addr, CredOptions *options);
        void Start();
        void Stop();
        void Run();
    public: //tag util
        static inline void *GenerateWriteTag(mtk_msgid_t msgid) {
            return reinterpret_cast<void *>((msgid << 8) + 1);
        }
        static inline void *GenerateReadTag(uint32_t connect_sequence_num) {
            return reinterpret_cast<void *>((connect_sequence_num << 8) + 0);
        }
        static inline bool IsWriteOperation(void *tag) {
            return (((uintptr_t)tag) & 0x1) != 0;
        }
        static inline bool IsReadOperation(void *tag) {
            return (((uintptr_t)tag) & 0x1) == 0;
        }
        static inline uint32_t ConnectSequenceFromReadTag(void *tag) {
            return (uint32_t)(((uintptr_t)tag) >> 8);
        }
    protected:
        //stream open/close
        void Close() {
            if (io_ != nullptr) {
                ASSERT(prev_io_ == nullptr);
                prev_io_ = io_.release();
            }
            if (context_ != nullptr) {
                delete context_;
            }
        }
        void Open() {
            Close();
            connect_sequence_num_++;
            context_ = new ClientContext();
            SetDeadline(*context_, UINT32_MAX);
            io_ = stub_->AsyncWrite(context_, &cq_, GenerateReadTag(connect_sequence_num_));
        }
        void HandleEvent(bool ok, void *tag);
        //about gpr_timespec
        static void SetDeadline(ClientContext &ctx, uint32_t duration_msec);
        static gpr_timespec GRPCTime(uint32_t duration_msec);
    };
    class RPCStream {
    public:
        class IClientDelegate {
        public:
            virtual mtk_cid_t Id() const = 0;
            virtual bool Ready() const = 0; //until this return true, RPCStream does nothing
            virtual bool AddPayload(SystemPayload::Connect &c) = 0;
            virtual bool OnOpenStream(mtk_result_t r, const char *p, mtk_size_t len) = 0;
            virtual mtk_time_t OnCloseStream(int reconnect_attempt) = 0;
            virtual void Poll() = 0;
        };
        enum NetworkStatus {
            DISCONNECT,
            CONNECTING,
            ESTABLISHED,
            INITIALIZING,
            CONNECT,
        };
        static Reply *DISCONNECT_EVENT;
        static Reply *ESTABLISHED_EVENT;
        static Request *ESTABLISH_REQUEST;
        static constexpr timespec_t TIMEOUT_DURATION = clock::sec(30); //30sec
        static Error *TIMEOUT_ERROR;
        typedef grpc::SslCredentialsOptions CredOptions;
        struct SEntry {
            typedef std::function<void (mtk_result_t, const char *, size_t)> Callback;
            timespec_t start_at_;
            Callback cb_;
            SEntry(Callback cb) : cb_(cb) { start_at_ = clock::now(); }
            inline void operator () (const Reply *rep, const Error *err) {
                if (err != nullptr) {
                    cb_(err->error_code(), err->payload().c_str(), err->payload().length());                
                } else {
                    cb_(rep->type(), rep->payload().c_str(), rep->payload().length());
                }
            }
        };
    protected:
        IClientDelegate *delegate_;
        ConcurrentQueue<Reply*> replys_;
        ConcurrentQueue<Request*> requests_;
        std::mutex reqmtx_;
        std::map<mtk_msgid_t, SEntry*> reqmap_;
        std::map<uint32_t, SEntry::Callback> notifymap_;
        bool restarting_;
        timespec_t last_checked_, reconnect_when_;
        ATOMIC_INT msgid_seed_;
        int reconnect_attempt_;
        NetworkStatus status_;
        IOThread iothr_;
        bool dump_;
    public:
        RPCStream(IClientDelegate *d) : delegate_(d),
            replys_(), requests_(), reqmtx_(), reqmap_(), notifymap_(), 
            restarting_(false), last_checked_(0), reconnect_when_(0), msgid_seed_(0), reconnect_attempt_(0), 
            status_(NetworkStatus::DISCONNECT), iothr_(*this), dump_(false) {
        };
        virtual ~RPCStream() {}
        mtk_cid_t Id() const { return delegate_->Id(); }
        void SetDump() { dump_ = true; }
    public:
        int Initialize(const char *addr, CredOptions *options);
        void Release();
        void Finalize();
        void Update();
        void Receive();
        void HandleEvent(bool ok, void *tag);
        void RegisterNotifyCB(uint32_t type, SEntry::Callback cb) { notifymap_[type] = cb; }
        //handshakers
        void StartWrite();
        void StartRead();
        inline bool IsConnected() const { return status_ == CONNECT; }
        inline mtk_msgid_t NewMsgId() {
            while (true) {
                int32_t expect = msgid_seed_.load();
                int32_t desired = expect + 1;
                if (desired >= 2000000000) {
                    desired = 1;
                }
#if defined(ANDROID)
                ASSERT(false);
                return (mtk_msgid_t)desired;
#else
                if (atomic_compare_exchange_weak(&msgid_seed_, &expect, desired)) {
                    return (mtk_msgid_t)desired;
                }
#endif
            }
            ASSERT(false);
            return 0;
        }
        inline bool IsConnecting() const { return status_ >= CONNECTING && status_ <= INITIALIZING; }
        timespec_t ReconnectWaitUsec();
        static timespec_t CalcReconnectWaitDuration(int n_attempt);
        static timespec_t CalcJitter(timespec_t base);
        static timespec_t Tick();
        void DrainQueue();
        void DrainRequestQueue();
        void ProcessReply();
        void ProcessTimeout(timespec_t now);
        inline bool PushReply(Reply *rep) { return replys_.enqueue(rep); }
        inline bool PopRequest(Request* &req) { return requests_.try_dequeue(req); }
        //call rpc via stream. not thread safe
        inline void Call(uint32_t type,
                  const char *buff, size_t len,
                  SEntry::Callback cb,
                  timespec_t timeout_msec = TIMEOUT_DURATION) {
            Request *msg = new Request();
            msg->set_type(type);
            msg->set_payload(buff, len);
            Call(msg, cb, timeout_msec);
        }
        inline void Call(Request *msg,
                  SEntry::Callback cb,
                  timespec_t timeout_msec) {
            if (status_ < NetworkStatus::ESTABLISHED) {
                ASSERT(false);
                return;
            }
            mtk_msgid_t msgid = NewMsgId();
            SEntry *ent = new SEntry(cb);
            msg->set_msgid(msgid);
            requests_.enqueue(msg);
            reqmtx_.lock();
            reqmap_[msgid] = ent;
            reqmtx_.unlock();
        }
        template <class SYSTEM_PAYLOAD>
        void Call(const SYSTEM_PAYLOAD &spl, SEntry::Callback cb) {
            char buffer[spl.ByteSize()];
            if (Codec::Pack(spl, (uint8_t *)buffer, spl.ByteSize()) < 0) {
                ASSERT(false);
                return;
            }
            Request *msg = new Request();
            msg->set_payload(buffer, spl.ByteSize());
            SetSystemPayloadKind<SYSTEM_PAYLOAD>(*msg);
            Call(msg, cb, TIMEOUT_DURATION);
        }
        template <class SYSTEM_PAYLOAD>
        void SetSystemPayloadKind(Request &req) { ASSERT(false); }
    protected:
        static void NopInternalCallback(::google::protobuf::Message &);
    };
    template <> void RPCStream::SetSystemPayloadKind<SystemPayload::Connect>(Request &req);
    template <> void RPCStream::SetSystemPayloadKind<SystemPayload::Ping>(Request &req);
}