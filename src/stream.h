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
    class IDuplexStreamDelegate {
    public:
        virtual mtk_cid_t Id() const = 0;
        virtual bool Valid() const = 0;
        virtual bool AddPayload(SystemPayload::Connect &c) = 0;
        virtual bool OnOpenStream(mtk_result_t r, const char *p, mtk_size_t len) = 0;
        virtual mtk_time_t OnCloseStream(int reconnect_attempt) = 0;
        virtual void Poll() = 0;
    };
    class DuplexStream {
    public:
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
        typedef Stream::Stub Stub;
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
        //following will be move to seperate module for reusing on server side.
        IDuplexStreamDelegate *delegate_;
        std::unique_ptr<Stub> stub_;
        ConcurrentQueue<Reply*> replys_;
        ConcurrentQueue<Request*> requests_;
        std::mutex reqmtx_;
        bool is_sending_;
        bool alive_, restarting_;
        std::unique_ptr<ClientAsyncReaderWriter<Request, Reply>> conn_;
        ClientAsyncReaderWriter<Request, Reply> *prev_conn_;
        std::map<mtk_msgid_t, SEntry*> reqmap_;
        std::map<uint32_t, SEntry::Callback> notifymap_;
        ClientContext *context_;
        Reply *reply_work_;
        CompletionQueue cq_;
        std::thread thr_;
        timespec_t last_checked_, reconnect_when_;
        ATOMIC_INT msgid_seed_;
        int reconnect_attempt_;
        uint32_t connect_sequence_num_;
        NetworkStatus status_;
        bool dump_;
    public:
        DuplexStream(IDuplexStreamDelegate *d) : delegate_(d),
            stub_(), replys_(), reqmtx_(), alive_(true), restarting_(false), reqmap_(), notifymap_(), cq_(), thr_(),
            last_checked_(0), reconnect_when_(0), msgid_seed_(0), reconnect_attempt_(0), connect_sequence_num_(0),
            status_(NetworkStatus::DISCONNECT), dump_(false) {
            is_sending_ = false;
            context_ = nullptr;
            reply_work_ = nullptr;
            prev_conn_ = nullptr;
        };
        virtual ~DuplexStream() {}
        uint64_t Id() const { return delegate_->Id(); }
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
        //stream open/close
        void Close() {
            if (conn_ != nullptr) {
                ASSERT(prev_conn_ == nullptr);
                prev_conn_ = conn_.release();
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
            conn_ = stub_->AsyncWrite(context_, &cq_, GenerateReadTag(connect_sequence_num_));
        }
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
        static void NopInternalCallback(::google::protobuf::Message &);
        //about gpr_timespec
        static void SetDeadline(ClientContext &ctx, uint32_t duration_msec);
        static gpr_timespec GRPCTime(uint32_t duration_msec);
    };
    template <> void DuplexStream::SetSystemPayloadKind<SystemPayload::Connect>(Request &req);
    template <> void DuplexStream::SetSystemPayloadKind<SystemPayload::Ping>(Request &req);
}