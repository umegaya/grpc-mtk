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
#include "logger.h"
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
        ClientContext *context_;
        std::unique_ptr<Reply> reply_work_;
        Status last_status_;
        uint32_t connect_sequence_num_;
        bool is_sending_, alive_, sending_shutdown_;
    public:
        IOThread(RPCStream &s) : owner_(s), thr_(), stub_(), io_(), cq_(), 
            context_(nullptr), reply_work_(), last_status_(), 
            connect_sequence_num_(0), is_sending_(false), alive_(true), sending_shutdown_(false) {}
        void Initialize(const char *addr, CredOptions *options);
        void Finalize();
        void Start();
        void Stop();
        void Run();
        inline grpc::string RemoteAddress() const { return context_->peer(); }
    public: //tag util
        static inline void *GenerateWriteTag(mtk_msgid_t msgid) {
            return reinterpret_cast<void *>((msgid << 8) + 1);
        }
        static inline void *GenerateReadTag(uint32_t connect_sequence_num) {
            return reinterpret_cast<void *>((connect_sequence_num << 8) + 0);
        }
        static inline void *GenerateCloseTag(uint32_t connect_sequence_num) {
            return reinterpret_cast<void *>((connect_sequence_num << 8) + 2);
        }
        static inline bool IsWriteOperation(void *tag) {
            return (((uintptr_t)tag) & 0xFF) == 1;
        }
        static inline bool IsReadOperation(void *tag) {
            return (((uintptr_t)tag) & 0xFF) == 0;
        }
        static inline bool IsCloseOperation(void *tag) {
            return (((uintptr_t)tag) & 0xFF) == 2;
        }
        static inline uint32_t ConnectSequenceFromReadTag(void *tag) {
            return (uint32_t)(((uintptr_t)tag) >> 8);
        }
        static inline uint32_t ConnectSequenceFromCloseTag(void *tag) {
            return ConnectSequenceFromReadTag(tag);
        }
    protected:
        //stream open/close
        void Close() {
            // recent grpc does not need this kind of hack anymore
            // if (io_ != nullptr) {
            //     ASSERT(prev_io_ == nullptr);
            //     //here, still old rpc result that not appeared in completion queue, may exist. 
            //     //so delete io here may cause crash. after new connection established, prev_io_ will be removed.
            //     prev_io_ = io_.release();
            // }
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

        //handling completion queue event
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
        static constexpr timespec_t USE_DEFAULT = clock::sec(0);
        static Error *TIMEOUT_ERROR, *BROKEN_PAYLOAD_ERROR, *NOT_CONNECT_ERROR;
        typedef grpc::SslCredentialsOptions CredOptions;
        struct SEntry {
            typedef std::function<void (mtk_result_t, const char *, size_t)> Callback;
            timespec_t start_at_;
            Callback cb_;
            SEntry(Callback cb) : cb_(cb) { start_at_ = clock::now(); }
            inline void operator () (const Reply *rep, const Error *err) {
                Respond(cb_, rep, err);
            }
            static inline void Respond(Callback cb, const Reply *rep, const Error *err) {
                if (err != nullptr) {
                    cb(err->error_code(), err->payload().c_str(), err->payload().length());                
                } else {
                    cb(rep->type(), rep->payload().c_str(), rep->payload().length());
                }
            }
        };
    protected:
        IClientDelegate *delegate_;
        ConcurrentQueue<Reply*> replys_;
        ConcurrentQueue<Request*> requests_;
        std::mutex reqmtx_;
        std::map<mtk_msgid_t, SEntry*> reqmap_;
        SEntry::Callback notifier_;
        bool restarting_;
        timespec_t reconnect_when_, default_timeout_;
        ATOMIC_INT msgid_seed_;
        int reconnect_attempt_;
        NetworkStatus status_;
        IOThread *iothr_;
        bool dump_;
    public:
        RPCStream(IClientDelegate *d) : delegate_(d),
            replys_(), requests_(), reqmtx_(), reqmap_(), notifier_(), 
            restarting_(false), reconnect_when_(0), default_timeout_(TIMEOUT_DURATION), 
            msgid_seed_(0), reconnect_attempt_(0), 
            status_(NetworkStatus::DISCONNECT), iothr_(new IOThread(*this)), dump_(false) {
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
        void RegisterNotifyCB(SEntry::Callback cb) { notifier_ = cb; }
        //handshakers
        void StartWrite();
        void StartRead();
        inline bool IsConnected() const { return status_ == CONNECT; }
        inline bool IsConnecting() const { return status_ >= CONNECTING && status_ <= INITIALIZING; }
        inline NetworkStatus Status() const { return status_; }
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
        timespec_t ReconnectWait();
        static timespec_t CalcReconnectWaitDuration(int n_attempt);
        static timespec_t CalcJitter(timespec_t base);
        static timespec_t Tick();
        void DrainQueue();
        void DrainRequestQueue();
        void ProcessReply();
        void ProcessTimeout(timespec_t now);
        inline bool PushReply(Reply *rep) { return replys_.enqueue(rep); }
        inline bool PopRequest(Request* &req) { return requests_.try_dequeue(req); }
        inline bool SendShutdownRequest() {
            auto *req = new Request();
            req->set_kind(Request::Close); 
            return requests_.enqueue(req); 
        }
        inline void SetDefaultTimeout(timespec_t to) { default_timeout_ = to; }
        inline void Call(uint32_t type,
                  const char *buff, size_t len,
                  SEntry::Callback cb,
                  timespec_t timeout_msec = USE_DEFAULT) {
            Request *msg = new Request();
            msg->set_type(type);
            msg->set_payload(buff, len);
            Call(msg, cb, timeout_msec == USE_DEFAULT ? default_timeout_ : timeout_msec);
        }
        inline void Call(Request *msg,
                  SEntry::Callback cb,
                  timespec_t timeout_msec) {
            if (status_ < NetworkStatus::ESTABLISHED) {
                SEntry::Respond(cb, nullptr, NOT_CONNECT_ERROR);
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
            uint8_t buffer[spl.ByteSizeLong()];
            if (Codec::Pack(spl, buffer, spl.ByteSizeLong()) < 0) {
                SEntry::Respond(cb, nullptr, BROKEN_PAYLOAD_ERROR);
                return;
            }
            Request *msg = new Request();
            msg->set_payload(reinterpret_cast<char *>(buffer), spl.ByteSizeLong());
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

    //holding/release reference to grpc library, to prevent grpc_init/shutdown from being repeated on the fly.
    //this is useful if application which embed mtk need to cleanup/initialize mtk environment repeatedly on runtime, 
    //but do not want to cause grpc library init/shutdown with that timing.
    void PinLibrary();
    void UnpinLibrary();
}
