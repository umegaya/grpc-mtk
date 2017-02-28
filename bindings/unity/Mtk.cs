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
    public delegate bool ConnectStartReadyCB();
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
        ServerReceiveCB on_svmsg;

        [FieldOffset(sizeof(IntPtr))]
        ServerAcceptCB on_accept;
    };
    struct Address {
        byte *host, *cert, *key, *ca;
    };
    struct ThreadConfig {
        uint n_reader, n_writer;
    };
    struct ServerConfig {
        ThreadConfig thread;
        Closure handler, acceptor;
        bool exclusive; //if true, caller thread of mtk_listen blocks
    };
    struct ClientConfig {
        ulong id;
        byte *payload;
        uint payload_len;
        Closure on_connect, on_close;
        ConnectStartReadyCB validate; //pending actual handshake until this returns true
    };

    //listener
    [DllImport (DllName)]
    private static extern unsafe void *mtk_listen(ref Address listen_at, ref ServerConfig conf);
    [DllImport (DllName)]
    private static extern unsafe void mtk_listen_stop(void *sv);

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
    public class Conn : IConn {
        System.IntPtr conn_;
        public Conn(System.IntPtr c) {
            conn_ = c;
        }
        public ulong Id { 
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
        public void Close() {
            unsafe { mtk_conn_close(conn_); }
        }
    }
    public class SVConn : IConn {
        System.IntPtr conn_;
        public SVConn(System.IntPtr c) {
            conn_ = c;
        }
        public ulong Id {
            get { unsafe { return mtk_svconn_cid(conn_); } }
        }
        public uint MsgId {
            get { unsafe { return mtk_svconn_msgid(conn_); } }
        }
        public void Reply(uint msgid, byte[] data) {
            unsafe { fixed (byte* d = data) { mtk_svconn_send(conn_, msgid, d, data.Length); } }
        }
        public void Task(uint type, byte[] data) {
            unsafe { fixed (byte* d = data) { mtk_svconn_task(conn_, type, d, data.Length); } }
        }
        public void Error(uint msgid, byte[] data) {
            unsafe { fixed (byte* d = data) { mtk_svconn_error(conn_, msgid, d, data.Length); } }
        }
        public void Notify(uint type, byte[] data) {
            unsafe { fixed (byte* d = data) { mtk_svconn_notify(conn_, type, d, data.Length); } }
        }
        public void Close() {
            unsafe { mtk_svconn_close(conn_); }
        }
    }
    public class Server {
        public struct AcceptReply {
            public ulong cid;
            public byte[] data;
        }
        System.IntPtr server_;
        public Server(System.IntPtr s) {
            server_ = s;
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
        ConnectStartReadyCB on_validate_;
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
        public ClientBuilder OnValidate(ConnectStartReadyCB cb) {
            on_validate_ = cb;
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
        ThreadConfig thread_;
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
        public ServerBuilder Worker(int n_reader, int n_writer) {
            thread_.n_reader = n_reader;
            thread_.n_writer = n_writer;
        }
        //these function will be called from another thread than Unity's main thread
        public ServerBuilder OnRecv(ServerReceiveCB cb, System.IntPtr arg = 0) {
            handler_.arg = arg;
            handler_.on_svmsg = cb;
        }
        public ServerBuilder OnAccept(ServerReceiveCB cb, System.IntPtr arg = 0) {
            acceptor_.arg = arg;
            acceptor_.on_accept = cb;
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
                        thread = thread_, exclusive = false,
                        acceptor = acceptor_, handler = handler_,
                    };
                    var s = new Server(mtk_listen(ref addr, ref conf));
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




