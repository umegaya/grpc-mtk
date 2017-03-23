using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Mtk {
    //dllname
#if UNITY_EDITOR || UNITY_ANDROID
    const string DllName = "shutup";
#elif UNITY_IPHONE
    const string DllName = "__Internal";
#else
    #error "invalid arch"
#endif

    //delegates
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate bool ClientReadyCB(void *arg);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void ClientRecvCB(void *arg, int type_or_error, byte *buf, uint buflen);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate bool ClientConnectCB(void *arg, ulong cid, byte *buf, uint buflen);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate ulong ClientCloseCB(void *arg, ulong cid, int connect_attempts);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate int ServerReceiveCB(void *arg, void *svconn, int type, byte *buf, uint buflen);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate ulong ServerAcceptCB(void *arg, void *svconn, ulong cid, byte *credential, uint credlen, char **pp_reply, uint *p_replen);
    
    //structs
    struct Closure {
        IntPtr arg;
        //union fields
        [FieldOffset(sizeof(IntPtr))]
        ClientRecvCB on_msg;

        [FieldOffset(sizeof(IntPtr))]
        ClientConnectCB on_connect;

        [FieldOffset(sizeof(IntPtr))]
        ClientCloseCB on_close;

        [FieldOffset(sizeof(IntPtr))]
        ClientReadyCB on_ready;

        [FieldOffset(sizeof(IntPtr))]
        ServerReceiveCB on_svmsg;

        [FieldOffset(sizeof(IntPtr))]
        ServerAcceptCB on_accept;
    };
    struct Address {
        byte *host, *cert, *key, *ca;
    };
    struct ServerConfig {
        uint n_worker;
        Closure handler, acceptor;
        bool exclusive; //if true, caller thread of mtk_listen blocks
        bool use_queue;
    };
    struct ClientConfig {
        ulong id;
        byte *payload;
        uint payload_len;
        Closure on_connect, on_close;
        ConnectStartReadyCB validate; //pending actual handshake until this returns true
    };
    struct ServerEvent {
        ulong lcid; // != 0 for accept event, 0 for recv event
        ulong cid;
        uint msgid;
        int result;
        uint datalen;
    };


    //util
    [DllImport (DllName)]
    private static extern unsafe bool mtk_queue_pop(void *q, ref System.IntPtr elem);
    [DllImport (DllName)]
    private static extern unsafe void mtk_queue_elem_free(void *q, void *elem);

    //listener
    [DllImport (DllName)]
    private static extern unsafe void mtk_listen(ref Address listen_at, ref ServerConfig conf, ref System.IntPtr sv);
    [DllImport (DllName)]
    private static extern unsafe void mtk_server_stop(void *sv);
    [DllImport (DllName)]
    private static extern unsafe void *mtk_server_queue(void *sv);

    //server conn operation
    [DllImport (DllName)]
    private static extern unsafe ulong mtk_svconn_cid(void *conn);
    [DllImport (DllName)]
    private static extern unsafe uint mtk_svconn_msgid(void *conn);
    [DllImport (DllName)]
    private static extern unsafe void mtk_svconn_send(void *conn, uint msgid, byte *data, uint datalen);
    [DllImport (DllName)]
    private static extern unsafe void mtk_svconn_notify(void *conn, uint type, byte *data, uint datalen);
    [DllImport (DllName)]
    private static extern unsafe void mtk_svconn_error(void *conn, uint msgid, byte *data, uint datalen);
    [DllImport (DllName)]
    private static extern unsafe void mtk_svconn_task(void *conn, uint type, byte *data, uint datalen);
    [DllImport (DllName)]
    private static extern unsafe void mtk_svconn_close(void *conn);
    [DllImport (DllName)]
    private static extern unsafe void mtk_svconn_finish_login(ulong login_cid, ulong cid, uint msgid, byte *data, uint datalen);
    [DllImport (DllName)]
    private static extern unsafe ulong mtk_svconn_defer_login(void *conn);

    //other server conn, using cid 
    [DllImport (DllName)]
    private static extern unsafe void mtk_cid_send(ulong cid, uint msgid, byte *data, uint datalen);
    [DllImport (DllName)]
    private static extern unsafe void mtk_cid_notify(ulong cid, uint type, byte *data, uint datalen);
    [DllImport (DllName)]
    private static extern unsafe void mtk_cid_error(ulong cid, uint msgid, byte *data, uint datalen);
    [DllImport (DllName)]
    private static extern unsafe void mtk_cid_task(ulong cid, uint type, byte *data, uint datalen);
    [DllImport (DllName)]
    private static extern unsafe void mtk_cid_close(ulong cid);

    //client conn
    [DllImport (DllName)]
    private static extern unsafe void *mtk_connect(ref Address connect_to, ref ClientConfig conf);
    [DllImport (DllName)]
    private static extern unsafe ulong mtk_conn_cid(void *conn);
    [DllImport (DllName)]
    private static extern unsafe void mtk_conn_poll(void *conn);
    [DllImport (DllName)]
    private static extern unsafe void mtk_conn_close(void *conn);
    [DllImport (DllName)]
    private static extern unsafe void mtk_conn_reset(void *conn); //this just restart connection, never destroy. 
    [DllImport (DllName)]
    private static extern unsafe void mtk_conn_send(void *conn, uint type, byte *data, uint datalen, Closure clsr);
    [DllImport (DllName)]
    private static extern unsafe void mtk_conn_watch(void *conn, uint type, Closure clsr);
    [DllImport (DllName)]
    private static extern unsafe bool mtk_conn_connected(void *conn);

    //wrapper
    public interface IConn {
        public ulong Id { get; }
        public void Close();
    }
    public interface ISVConn : IConn {
        public uint MsgId { get; }
        public void Reply(uint msgid, byte[] data);
        public void Task(uint type, byte[] data);
        public void Error(uint msgid, byte[] data);
        public void Notify(uint type, byte[] data);
        public void Close();     
    }
    public class Conn : IConn {
        System.IntPtr conn_;
        public Conn(System.IntPtr c) {
            conn_ = c;
        }
        public override ulong Id { 
            get { unsafe { return mtk_conn_cid(conn_); } } 
        }
        public bool IsConnected {
            get { unsafe { return mtk_conn_connected(conn_); } }
        }
        public void Send(uint type, byte[] data, Closure clsr) {
            unsafe { fixed (byte* d = data) { mtk_conn_send(conn_, type, d, data.Length, clsr); } }
        }
        public void Watch(uint type, Closure clsr) {
            unsafe { mtk_conn_watch(conn_, type, clsr); }
        }
        public void Poll() {
            unsafe { mtk_conn_poll(conn_); }
        }
        public void Reset() {
            unsafe { mtk_conn_reset(conn_); }
        }
        public override void Close() {
            unsafe { mtk_conn_close(conn_); }
        }
    }
    public class SVConn : ISVConn {
        System.IntPtr conn_;
        public SVConn(System.IntPtr c) {
            conn_ = c;
        }
        public override ulong Id {
            get { unsafe { return mtk_svconn_cid(conn_); } }
        }
        public override uint MsgId {
            get { unsafe { return mtk_svconn_msgid(conn_); } }
        }
        public override　void Reply(uint msgid, byte[] data) {
            unsafe { fixed (byte* d = data) { mtk_svconn_send(conn_, msgid, d, data.Length); } }
        }
        public override　void Task(uint type, byte[] data) {
            unsafe { fixed (byte* d = data) { mtk_svconn_task(conn_, type, d, data.Length); } }
        }
        public override void Error(uint msgid, byte[] data) {
            unsafe { fixed (byte* d = data) { mtk_svconn_error(conn_, msgid, d, data.Length); } }
        }
        public override void Notify(uint type, byte[] data) {
            unsafe { fixed (byte* d = data) { mtk_svconn_notify(conn_, type, d, data.Length); } }
        }
        public override　void Close() {
            unsafe { mtk_svconn_close(conn_); }
        }
    }
    public class CidConn : ISVConn {
        System.IntPtr cid_;
        uint msgid_;
        public SVConn(System.IntPtr cid, uint msgid) {
            cid_ = cid;
            msgid_ = msgid;
        }
        public ulong Id {
            get { unsafe { return cid_; } }
        }
        virtual public uint MsgId {
            get { unsafe { return msgid_; } }
        }
        virtual public void Reply(uint msgid, byte[] data) {
            unsafe { fixed (byte* d = data) { mtk_cid_send(cid_, msgid, d, data.Length); } }
        }
        virtual public void Task(uint type, byte[] data) {
            unsafe { fixed (byte* d = data) { mtk_cid_task(cid_, type, d, data.Length); } }
        }
        virtual public void Error(uint msgid, byte[] data) {
            unsafe { fixed (byte* d = data) { mtk_cid_error(cid_, msgid, d, data.Length); } }
        }
        virtual public void Notify(uint type, byte[] data) {
            unsafe { fixed (byte* d = data) { mtk_cid_notify(cid_, type, d, data.Length); } }
        }
        virtual public void Close() {
            unsafe { mtk_cid_close(cid_); }
        }
    }
    public class Server {
        public struct AcceptReply {
            public ulong cid;
            public byte[] data;
        }
        System.IntPtr server_;
        System.IntPtr queue_;
        public Server(System.IntPtr s) {
            server_ = s;
            unsafe {
                queue_ = mtk_server_queue(server_);
            }
        }
        public void Process(IServerLogic logic) {
            System.IntPtr elem;
            while (sv_.PopEvent(ref elem)) {
                ServerEvent *ev = (ServerEvent *)elem;
                if (ev->lcid != 0) {
                    var ret = new byte[ev->datalen];
                    Marshal.Copy(((byte *)ev) + sizeof(ServerEvent), ret, 0, ev->datalen);
                    var rep = logic.OnAccept(ev->cid, ret);
                    mtk_svconn_finish_login(ev->lcid, rep.cid, ev->msgid, rep.data, rep.data.Length);
                } else {
                    var c = new CidConn(ev->cid, ev->msgid);
                    var ret = new byte[ev->datalen];
                    Marshal.Copy(((byte *)ev) + sizeof(ServerEvent), ret, 0, ev->datalen);
                    logic.OnRecv(c, ev->result, ret);
                }
                sv_.FreeEvent(elem);
            }
        }
    }
    class Builder {
        protected string host_, cert_, key_, ca_;
        public Builder() {}
        public Address Addr { get { return addr_; } }
        public Builder ListenAt(string host) {
            host_ = host;
            return this;
        }
        public Builder Certs(string cert, string key, string ca) {
            cert_ = cert;
            key_ = key;
            ca_ = ca;
            return this;            
        }
    }
    public class ClientBuilder : Builder {
        ulong id_;
        string payload_;
        Closure on_connect_, on_close_;
        ClientReadyCB on_ready_;
        public ClientBuilder() {}
        public ClientBuilder ListenAt(string at) {
            base.ListenAt(at);
            return this;
        }
        public ClientBuilder Certs(string cert, string key, string ca) {
            base.Certs(cert, key, ca);
            return this;            
        }
        public ClientBuilder Credential(ulong id, string data) {
            id_ = id;
            payload_ = data;
        }
        //these function will be called from same thread as Unity's main thread
        public ClientBuilder OnClose(ClientCloseCB cb, System.IntPtr arg = 0) {
            on_close_.arg = arg;
            on_close_.on_close = cb;
        }
        public ClientBuilder OnConnect(ClientConnectCB cb, System.IntPtr arg = 0) {
            on_connect_.arg = arg;
            on_connect_.on_connect = cb;
        }
        public ClientBuilder OnReady(ClientReadyCB cb) {
            on_ready_ = cb;
        }
        public Conn Build() {
            var b_host = System.Text.Encoding.UTF8.GetBytes(host_ + "\0"));
            var b_cert = System.Text.Encoding.UTF8.GetBytes(cert_ + "\0"));
            var b_key = System.Text.Encoding.UTF8.GetBytes(key_ + "\0"));
            var b_ca = System.Text.Encoding.UTF8.GetBytes(ca_ + "\0"));
            var b_payload = System.Text.Encoding.UTF8.GetBytes(payload_ + "\0"));
            unsafe {
                fixed (byte* h = b_host, c = b_cert, k = b_key, ca = b_ca, p = b_payload) {
                    Address addr = new Address { host = h, cert = c, key = k, ca = b_ca };
                    ClientConfig conf = new ClientConfig { 
                        id = id_, payload = p, payload_len = payload_.Length,
                        on_connect = on_connect_, on_close = on_close_,
                    };
                    var c = new Conn(mtk_connect(ref addr, ref conf));
                    Core.Instance().ConnMap[id_] = c;
                    return c;
                }
            }
        }
    }
    public class ServerBuilder : Builder {
        uint n_worker_;
        Closure handler_, acceptor_:
        public ServerBuilder() {}
        public ServerBuilder ListenAt(string at) {
            base.ListenAt(at);
            return this;
        }
        public ServerBuilder Certs(string cert, string key, string ca) {
            base.Certs(cert, key, ca);
            return this;
        }
        public ServerBuilder Worker(int n_worker) {
            n_worker_ = n_worker;
            return this;
        }
        static public int OnRecv(void *arg, void *svconn, int type, byte *buf, uint buflen) {
            return 0;
        }
        static public ulong OnAccept(void *arg, void *svconn, ulong cid, byte *credential, uint credlen, char **pp_reply, uint *p_replen) {
            return 0;
        }
        static Closure Nop() {
            Closure cl = new Closure();
            cl.handler_ = 
        }
        public Server Build() {
            var b_host = System.Text.Encoding.UTF8.GetBytes(host_ + "\0"));
            var b_cert = System.Text.Encoding.UTF8.GetBytes(cert_ + "\0"));
            var b_key = System.Text.Encoding.UTF8.GetBytes(key_ + "\0"));
            var b_ca = System.Text.Encoding.UTF8.GetBytes(ca_ + "\0"));
            unsafe {
                fixed (byte* h = b_host, c = b_cert, k = b_key, ca = b_ca) {
                    Address addr = new Address { host = h, cert = c, key = k, ca = b_ca };
                    ServerConfig conf = new ServerConfig { 
                        n_worker = n_worker_, exclusive = false,
                        use_queue = true,
                    };
                    conf.handler.arg = 0;
                    conf.handler.on_svmsg = OnRecv;
                    conf.acceptor.arg = 0;
                    conf.acceptor.on_svmsg = OnAccept;
                    
                    System.IntPtr svp;
                    mtk_listen(ref addr, ref conf, ref svp)
                    var s = new Server(svp);
                    Core.Instance().ServerMap[host_] = s;
                    return s;
                }
            }
        }
    }
    public class Core {
        static Core instance_ = null;
        public Dictionary<ulong, Conn> ConnMap { get; set; }
        public Dictionary<string, Server> ServerMap { get; set; }
        static public Core Instance() {
            if (instance_ == null) {
                instance_ = new Core();
            }
            return instance_;
        }
        Core() {
            ConnMap = new Dictionary<ulong, Conn>();
            ServerMap = new Dictionary<string Server>();
        }
    };
}





