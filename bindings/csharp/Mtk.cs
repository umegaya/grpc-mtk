using System.Collections.Generic;
using System.Runtime.InteropServices;
using Marshal = System.Runtime.InteropServices.Marshal;

namespace Mtk {
    public class Core {
        //dllname
    #if UNITY_EDITOR || UNITY_ANDROID
        const string DllName = "mtk";
    #elif UNITY_IPHONE
        const string DllName = "__Internal";
    #else
        #error "invalid arch"
    #endif

        //delegates
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate bool ClientReadyCB(System.IntPtr arg);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate void ClientRecvCB(System.IntPtr arg, int type_or_error, byte *buf, uint buflen);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate bool ClientConnectCB(System.IntPtr arg, ulong cid, byte *buf, uint buflen);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate ulong ClientCloseCB(System.IntPtr arg, ulong cid, int connect_attempts);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int ServerReceiveCB(System.IntPtr arg, System.IntPtr svconn, int type, byte *buf, uint buflen);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate ulong ServerAcceptCB(System.IntPtr arg, System.IntPtr svconn, ulong cid, byte *credential, uint credlen, char **pp_reply, uint *p_replen);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void ServerCloseCB(System.IntPtr arg, ulong cid);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void LogWriteCB(string buf, System.IntPtr len);
        
        //structs
        public struct Closure {
            /*[System.Runtime.InteropServices.StructLayout(LayoutKind.Explicit)]
            public struct UCallback {
                //union fields
                [FieldOffset(0)]
                public ClientRecvCB on_msg;

                [FieldOffset(0)]
                public ClientConnectCB on_connect;

                [FieldOffset(0)]
                public ClientCloseCB on_close;

                [FieldOffset(0)]
                public ClientReadyCB on_ready;

                [FieldOffset(0)]
                public ServerReceiveCB on_svmsg;

                [FieldOffset(0)]
                public ServerAcceptCB on_accept;

                [FieldOffset(0)]
                public ServerCloseCB on_svclose;
            };*/

            public System.IntPtr arg;
            //TODO: add type check. only above unmanaged function pointers are allowed.
            public System.IntPtr cb;
        };
        public struct Address {
            public System.IntPtr host, cert, key, ca;
        };
        struct ServerConfig {
            public uint n_worker;
            public Closure handler, acceptor, closer;
            public bool exclusive; //if true, caller thread of mtk_listen blocks
            public bool use_queue;
        };
        struct ClientConfig {
            public ulong id;
            public System.IntPtr payload;
            public uint payload_len;
            public Closure on_connect, on_close, on_ready;
        };
        struct ServerEvent {
            public ulong lcid; // != 0 for accept event, 0 for recv event
            public ulong cid;
            public uint msgid;
            public int result;
            public uint datalen;
        };


        //util
        [DllImport (DllName)]
        private static extern unsafe bool mtk_queue_pop(System.IntPtr q, ref System.IntPtr elem);
        [DllImport (DllName)]
        private static extern unsafe void mtk_queue_elem_free(System.IntPtr q, System.IntPtr elem);
        [DllImport (DllName)]
        private static extern unsafe void mtk_log_config(string name, LogWriteCB writer);

        //listener
        [DllImport (DllName)]
        private static extern unsafe void mtk_listen(ref Address listen_at, ref ServerConfig conf, ref System.IntPtr sv);
        [DllImport (DllName)]
        private static extern unsafe void mtk_server_stop(System.IntPtr sv);
        [DllImport (DllName)]
        private static extern unsafe System.IntPtr mtk_server_queue(System.IntPtr sv);

        //server conn operation
        [DllImport (DllName)]
        private static extern unsafe ulong mtk_svconn_cid(System.IntPtr conn);
        [DllImport (DllName)]
        private static extern unsafe uint mtk_svconn_msgid(System.IntPtr conn);
        [DllImport (DllName)]
        private static extern unsafe void mtk_svconn_send(System.IntPtr conn, uint msgid, byte *data, uint datalen);
        [DllImport (DllName)]
        private static extern unsafe void mtk_svconn_notify(System.IntPtr conn, uint type, byte *data, uint datalen);
        [DllImport (DllName)]
        private static extern unsafe void mtk_svconn_error(System.IntPtr conn, uint msgid, byte *data, uint datalen);
        [DllImport (DllName)]
        private static extern unsafe void mtk_svconn_task(System.IntPtr conn, uint type, byte *data, uint datalen);
        [DllImport (DllName)]
        private static extern unsafe void mtk_svconn_close(System.IntPtr conn);
        [DllImport (DllName)]
        private static extern unsafe void mtk_svconn_finish_login(ulong login_cid, ulong cid, uint msgid, byte *data, uint datalen);
        [DllImport (DllName)]
        private static extern unsafe ulong mtk_svconn_defer_login(System.IntPtr conn);

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
        private static extern unsafe System.IntPtr mtk_connect(ref Address connect_to, ref ClientConfig conf);
        [DllImport (DllName)]
        private static extern unsafe ulong mtk_conn_cid(System.IntPtr conn);
        [DllImport (DllName)]
        private static extern unsafe void mtk_conn_poll(System.IntPtr conn);
        [DllImport (DllName)]
        private static extern unsafe void mtk_conn_close(System.IntPtr conn);
        [DllImport (DllName)]
        private static extern unsafe void mtk_conn_reset(System.IntPtr conn); //this just restart connection, never destroy. 
        [DllImport (DllName)]
        private static extern unsafe void mtk_conn_send(System.IntPtr conn, uint type, byte *data, uint datalen, Closure clsr);
        [DllImport (DllName)]
        private static extern unsafe void mtk_conn_watch(System.IntPtr conn, uint type, Closure clsr);
        [DllImport (DllName)]
        private static extern unsafe bool mtk_conn_connected(System.IntPtr conn);

        //wrapper
        public interface IConn {
            ulong Id { get; }
            void Close();
        }
        public interface ISVConn : IConn {
            uint MsgId { get; }
            void Reply(uint msgid, byte[] data);
            void Task(uint type, byte[] data);
            void Error(uint msgid, byte[] data);
            void Notify(uint type, byte[] data);
            void Close();     
        }
        public interface IServerLogic {
            ulong OnAccept(ulong cid, byte[] data, out byte[] rep);
            int OnRecv(ISVConn c, int type, byte[] data);
            void OnClose(ulong cid);
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
                unsafe { fixed (byte* d = data) { mtk_conn_send(conn_, type, d, (uint)data.Length, clsr); } }
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
        public class SVConn : ISVConn {
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
                unsafe { fixed (byte* d = data) { mtk_svconn_send(conn_, msgid, d, (uint)data.Length); } }
            }
            public void Task(uint type, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_svconn_task(conn_, type, d, (uint)data.Length); } }
            }
            public void Error(uint msgid, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_svconn_error(conn_, msgid, d, (uint)data.Length); } }
            }
            public void Notify(uint type, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_svconn_notify(conn_, type, d, (uint)data.Length); } }
            }
            public void Close() {
                unsafe { mtk_svconn_close(conn_); }
            }
        }
        public class CidConn : ISVConn {
            ulong cid_;
            uint msgid_;
            public CidConn(ulong cid, uint msgid) {
                cid_ = cid;
                msgid_ = msgid;
            }
            public ulong Id {
                get { unsafe { return cid_; } }
            }
            public uint MsgId {
                get { unsafe { return msgid_; } }
            }
            public void Reply(uint msgid, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_cid_send(cid_, msgid, d, (uint)data.Length); } }
            }
            public void Task(uint type, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_cid_task(cid_, type, d, (uint)data.Length); } }
            }
            public void Error(uint msgid, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_cid_error(cid_, msgid, d, (uint)data.Length); } }
            }
            public void Notify(uint type, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_cid_notify(cid_, type, d, (uint)data.Length); } }
            }
            public void Close() {
                unsafe { mtk_cid_close(cid_); }
            }
        }
        public class Server {
            System.IntPtr server_;
            System.IntPtr queue_;
            public Server(System.IntPtr s) {
                server_ = s;
                unsafe {
                    queue_ = mtk_server_queue(server_);
                }
            }
            public unsafe void Process(IServerLogic logic) {
                System.IntPtr elem = new System.IntPtr();
                while (mtk_queue_pop(queue_, ref elem)) {
                    ServerEvent *ev = (ServerEvent *)elem;
                    if (ev->lcid != 0) {
                        var ret = new byte[ev->datalen];
                        byte[] rep;
                        Marshal.Copy((System.IntPtr)(((byte *)ev) + sizeof(ServerEvent)), ret, 0, (int)ev->datalen);
                        var cid = logic.OnAccept(ev->cid, ret, out rep);
                        fixed (byte *pb = rep) {
                            mtk_svconn_finish_login(ev->lcid, cid, ev->msgid, pb, (uint)rep.Length);
                        }
                    } else if (ev->msgid != 0) {
                        var c = new CidConn(ev->cid, ev->msgid);
                        var ret = new byte[ev->datalen];
                        Marshal.Copy((System.IntPtr)(((byte *)ev) + sizeof(ServerEvent)), ret, 0, (int)ev->datalen);
                        logic.OnRecv(c, ev->result, ret);
                    } else {
                        logic.OnClose(ev->cid);
                    }
                    mtk_queue_elem_free(queue_, elem);
                }
            }
        }
        public class Builder {
            protected string host_, cert_, key_, ca_;
            public Builder() {}
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
            Closure on_connect_, on_close_, on_ready_;
            public ClientBuilder() {}
            public ClientBuilder ConnectTo(string at) {
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
                return this;
            }
            //these function will be called from same thread as Unity's main thread
            public ClientBuilder OnClose(ClientCloseCB cb) {
                on_close_.arg = new System.IntPtr(0);
                on_close_.cb = Marshal.GetFunctionPointerForDelegate(cb);
                return this;
            }
            public ClientBuilder OnConnect(ClientConnectCB cb) {
                on_connect_.arg = new System.IntPtr(0);
                on_connect_.cb = Marshal.GetFunctionPointerForDelegate(cb);
                return this;
            }
            public ClientBuilder OnReady(ClientReadyCB cb) {
                on_ready_.arg = new System.IntPtr(0);
                on_ready_.cb = Marshal.GetFunctionPointerForDelegate(cb);
                return this;
            }
            public Conn Build() {
                var b_host = System.Text.Encoding.UTF8.GetBytes(host_ + "\0");
                var b_cert = System.Text.Encoding.UTF8.GetBytes(cert_ + "\0");
                var b_key = System.Text.Encoding.UTF8.GetBytes(key_ + "\0");
                var b_ca = System.Text.Encoding.UTF8.GetBytes(ca_ + "\0");
                var b_payload = System.Text.Encoding.UTF8.GetBytes(payload_ + "\0");
                unsafe {
                    fixed (byte* h = b_host, c = b_cert, k = b_key, a = b_ca, p = b_payload) {
                        Address addr = new Address { host = (System.IntPtr)h, cert = (System.IntPtr)c, key = (System.IntPtr)k, ca = (System.IntPtr)a };
                        ClientConfig conf = new ClientConfig { 
                            id = id_, payload = (System.IntPtr)p, payload_len = (uint)payload_.Length,
                            on_connect = on_connect_, on_close = on_close_,
                        };
                        var conn = new Conn(mtk_connect(ref addr, ref conf));
                        Core.Instance().ConnMap[id_] = conn;
                        return conn;
                    }
                }
            }
        }
        public class ServerBuilder : Builder {
            uint n_worker_;
            Closure handler_, acceptor_;
            public ServerBuilder() {}
            public ServerBuilder ListenAt(string at) {
                base.ListenAt(at);
                return this;
            }
            public ServerBuilder Certs(string cert, string key, string ca) {
                base.Certs(cert, key, ca);
                return this;
            }
            public ServerBuilder Worker(uint n_worker) {
                n_worker_ = n_worker;
                return this;
            }
            public Server Build() {
                var b_host = System.Text.Encoding.UTF8.GetBytes(host_ + "\0");
                var b_cert = System.Text.Encoding.UTF8.GetBytes(cert_ + "\0");
                var b_key = System.Text.Encoding.UTF8.GetBytes(key_ + "\0");
                var b_ca = System.Text.Encoding.UTF8.GetBytes(ca_ + "\0");
                unsafe {
                    fixed (byte* h = b_host, c = b_cert, k = b_key, a = b_ca) {
                        Address addr = new Address { host = (System.IntPtr)h, cert = (System.IntPtr)c, key = (System.IntPtr)k, ca = (System.IntPtr)a };
                        ServerConfig conf = new ServerConfig { 
                            n_worker = n_worker_, exclusive = false,
                            use_queue = true,
                        };
                        conf.handler.arg = new System.IntPtr(0);
                        conf.handler.cb = new System.IntPtr(0);
                        conf.acceptor.arg = new System.IntPtr(0);
                        conf.acceptor.cb = new System.IntPtr(0);
                        conf.closer.arg = new System.IntPtr(0);
                        conf.closer.cb = new System.IntPtr(0);
                        
                        System.IntPtr svp = new System.IntPtr();
                        mtk_listen(ref addr, ref conf, ref svp);
                        var s = new Server(svp);
                        Core.Instance().ServerMap[host_] = s;
                        return s;
                    }
                }
            }
        }

        static Core instance_ = null;
        public Dictionary<ulong, Conn> ConnMap { get; set; }
        public Dictionary<string, Server> ServerMap { get; set; }
        static public Core Instance() {
            if (instance_ == null) {
                instance_ = new Core();
            }
            return instance_;
        }
        public static void InitLogger(string name, LogWriteCB writer) {
            mtk_log_config(name, writer);
        }
        Core() {
            ConnMap = new Dictionary<ulong, Conn>();
            ServerMap = new Dictionary<string, Server>();
        }
    }
}





