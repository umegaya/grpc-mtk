#pragma once

#include "mtk.h"
#include "proto/src/mtk.grpc.pb.h"
#include "codec.h"
#include "timespec.h"
#include <MoodyCamel/concurrentqueue.h>
#include <thread>
#include <mutex>
#include <functional>
#include "atomic_compat.h"
#if !defined(ASSERT)
#if defined(DEBUG)
#include <assert.h>
#define ASSERT(cond) assert(cond)
#else
#define ASSERT(cond)
#endif
#endif

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
        virtual bool AddPayload(SystemPayload::Connect &c, int stream_idx) = 0;
        virtual void OnConnect(mtk_result_t r, const char *p, size_t len) = 0;
        virtual void Poll() = 0;
    };
    class DuplexStream {
    public:
        enum NetworkStatus {
            DISCONNECT,
            CONNECTING,
            ESTABLISHED,
            REGISTER,
            INITIALIZING,
            CONNECT,
        };
        enum StreamIndex {
            WRITE,
            READ,
            NUM_STREAM,
        };
        static Reply *DISCONNECT_EVENT;
        static Reply *ESTABLISHED_EVENT;
        static Request *ESTABLISH_REQUEST;
        static constexpr timespec_t TIMEOUT_DURATION = clock::sec(30); //30sec
        static Error *TIMEOUT_ERROR;
        typedef Stream::Stub Stub;
        typedef grpc::SslCredentialsOptions CredOptions;
        typedef grpc::SslServerCredentialsOptions ServerCredOptions;
        struct SEntry {
            typedef std::function<void (mtk_result_t, const char *, size_t)> Callback;
            timespec_t start_at_;
            Callback cb_;
            SEntry(Callback cb) : cb_(cb) { start_at_ = clock::now(); }
            inline void operator () (const Reply *rep, const Error *err) {
                if (rep != nullptr) {
                    cb_(rep->type(), rep->payload().c_str(), rep->payload().length());
                } else {
                    cb_(err->error_code(), err->payload().c_str(), err->payload().length());                
                }
            }
        };
    protected:
        //following will be move to seperate module for reusing on server side.
        IDuplexStreamDelegate *delegate_;
        std::unique_ptr<Stub> stub_;
        ConcurrentQueue<Reply*> replys_;
        ConcurrentQueue<Request*> requests_[NUM_STREAM];
        std::mutex reqmtx_;
        bool is_sending_[NUM_STREAM];
        bool alive_, restarting_;
        std::unique_ptr<ClientAsyncReaderWriter<Request, Reply>> conn_[NUM_STREAM];
        ClientAsyncReaderWriter<Request, Reply> *prev_conn_[NUM_STREAM];
        std::map<mtk_msgid_t, SEntry*> reqmap_;
        std::map<uint32_t, SEntry::Callback> notifymap_;
        ClientContext *context_[NUM_STREAM];
        Reply *reply_work_[NUM_STREAM];
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
            memset(is_sending_, 0, sizeof(is_sending_));
            memset(context_, 0, sizeof(context_));
            memset(reply_work_, 0, sizeof(reply_work_));
            memset(prev_conn_, 0, sizeof(prev_conn_));
        };
        ~DuplexStream() {}
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
            for (int i = 0; i < NUM_STREAM; i++) {
                if (conn_[i] != nullptr) {
                    ASSERT(prev_conn_[i] == nullptr);
                    prev_conn_[i] = conn_[i].release();
                }
                if (context_[i] != nullptr) {
                    delete context_[i];
                }
            }
        }
        void Open() {
            Close();
            connect_sequence_num_++;
            context_[WRITE] = new ClientContext();
            context_[READ] = new ClientContext();
            SetDeadline(*context_[WRITE], UINT32_MAX);
            SetDeadline(*context_[READ], UINT32_MAX);
            conn_[WRITE] = stub_->AsyncWrite(context_[WRITE], &cq_, GenerateReadTag(connect_sequence_num_, WRITE));
            conn_[READ] = stub_->AsyncRead(context_[READ], &cq_, GenerateReadTag(connect_sequence_num_, READ));
        }
        //call rpc via stream. not thread safe
        void Call(uint32_t type,
                  const char *buff, size_t len,
                  SEntry::Callback cb,
                  uint32_t timeout_msec = 30000,
                  StreamIndex sidx = WRITE, 
                  Request::Kind kind = Request::Normal) {
            if (status_ < NetworkStatus::ESTABLISHED) {
                ASSERT(false);
                return;
            }
            mtk_msgid_t msgid = NewMsgId();
            SEntry *ent = new SEntry(cb);
            Request *msg = new Request();
            msg->set_type(type);
            msg->set_msgid(msgid);
            msg->set_payload(buff, len);
            msg->set_kind(kind);
            requests_[sidx].enqueue(msg);
            reqmtx_.lock();
            reqmap_[msgid] = ent;
            reqmtx_.unlock();
        }
        template <class PAYLOAD>
        void Call(Request::Kind kind, const PAYLOAD &spl, SEntry::Callback cb, StreamIndex sidx) {
            char buffer[spl.ByteSize()];
            int len = Codec::Pack(spl, (uint8_t *)buffer, spl.ByteSize());
            if (len < 0) {
                ASSERT(false);
                return;
            }
            Call(0, buffer, spl.ByteSize(), cb, UINT32_MAX, sidx, kind);
        }
    protected:
        static inline void *GenerateWriteTag(mtk_msgid_t msgid, StreamIndex idx) {
            return reinterpret_cast<void *>((msgid << 8) + (uint8_t)(idx << 1) + 1);
        }
        static inline void *GenerateReadTag(uint32_t connect_sequence_num, StreamIndex idx) {
            return reinterpret_cast<void *>((connect_sequence_num << 8) + (uint8_t)(idx << 1) + 0);
        }
        static inline StreamIndex StreamFromWriteTag(void *tag) {
            uintptr_t tmp = (((uintptr_t)tag) & 0xFF);
            if ((tmp & 1) == 1) { return (StreamIndex)(tmp >> 1); }
            return NUM_STREAM;
        }
        static inline StreamIndex StreamFromReadTag(void *tag) {
            uintptr_t tmp = (((uintptr_t)tag) & 0xFF);
            if ((tmp & 1) == 0) { return (StreamIndex)(tmp >> 1); }
            return NUM_STREAM;
        }
        static inline uint32_t ConnectSequenceFromReadTag(void *tag) {
            return (uint32_t)(((uintptr_t)tag) >> 8);
        }
        static void NopInternalCallback(::google::protobuf::Message &);
        //about gpr_timespec
        static void SetDeadline(ClientContext &ctx, uint32_t duration_msec);
        static gpr_timespec GRPCTime(uint32_t duration_msec);
    };
}