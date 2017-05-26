using System.Collections.Generic;
using System.Runtime.InteropServices;
using AOT;
using Marshal = System.Runtime.InteropServices.Marshal;

namespace Mtk {
    public partial class Core {
        //dllname
    #if UNITY_EDITOR || UNITY_ANDROID
        const string DllName = "mtk";
    #elif UNITY_IPHONE || MTKSV
        const string DllName = "__Internal";
    #else
        #error "unsupported platform"
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
        public delegate ulong ClientStartCB(System.IntPtr arg, System.IntPtr slice);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate int ServerReceiveCB(System.IntPtr arg, System.IntPtr svconn, int type, byte *buf, uint buflen);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public unsafe delegate ulong ServerAcceptCB(System.IntPtr arg, System.IntPtr svconn, ulong cid, byte *credential, uint credlen, System.IntPtr slice);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void ServerCloseCB(System.IntPtr arg, ulong cid);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void LogWriteCB(string buf, System.IntPtr len, bool need_flush);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void DestroyPointerCB(System.IntPtr ptr);
        
        //structs
        public struct Closure {
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
            [MarshalAs(UnmanagedType.I1)] public bool exclusive; //if true, caller thread of mtk_listen blocks
            [MarshalAs(UnmanagedType.I1)] public bool use_queue;
        };
        struct ClientConfig {
            public Closure on_connect, on_close, on_ready, on_start;
        };
        struct ServerEvent {
            public ulong lcid; // != 0 for accept event, 0 for recv event
            public ulong cid;
            public uint msgid;
            public int result;
            public uint datalen;
        };
        static uint SERVER_EVENT_TRUE_SIZE = 28;
        public enum LogLevel {
            Trace,
            Debug,
            Info,
            Warn,
            Error,
            Fatal,            
            Report,
        };

        /*
            TODO: some of below entry points should be replaced internal call (really called high freqency, like mtk_svconn_XXX)
            see http://forcedtoadmin.blogspot.jp/2014/04/mono-unmanaged-calls-performance.html 
        */

        //util
        [DllImport (DllName)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern unsafe bool mtk_queue_pop(System.IntPtr q, ref System.IntPtr elem);
        [DllImport (DllName)]
        private static extern unsafe void mtk_queue_elem_free(System.IntPtr q, System.IntPtr elem);
        [DllImport (DllName)]
        private static extern ulong mtk_time();
        [DllImport (DllName)]
        private static extern ulong mtk_second();
        [DllImport (DllName)]
        private static extern void mtk_pause(ulong duration);
        [DllImport (DllName)]
        private static extern unsafe void mtk_slice_put(System.IntPtr slice, byte *data, uint datalen);
        [DllImport (DllName)]
        private static extern unsafe void mtk_lib_ref();
        [DllImport (DllName)]
        private static extern unsafe void mtk_lib_unref();

        [DllImport (DllName)]
        private static extern void mtk_log(int lv, [MarshalAs(UnmanagedType.LPStr)]string str);
        [DllImport (DllName)]
        private static extern void mtk_log_config([MarshalAs(UnmanagedType.LPStr)]string name, LogWriteCB writer);

        //listener
        [DllImport (DllName)]
        private static extern unsafe void mtk_listen(ref Address listen_at, ref ServerConfig conf, ref System.IntPtr sv);
        [DllImport (DllName)]
        private static extern unsafe System.IntPtr mtk_server_queue(System.IntPtr sv);
        [DllImport (DllName)]
        private static extern unsafe void mtk_server_join(System.IntPtr sv);
#if MTKSV
        [DllImport (DllName)]
        private static extern unsafe System.IntPtr mtkdn_server(ref Address listen_at, ref ServerConfig conf);
#endif

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
        private static extern unsafe void mtk_svconn_putctx(System.IntPtr conn, System.IntPtr ctx, DestroyPointerCB dtor);
        [DllImport (DllName)]
        private static extern unsafe System.IntPtr mtk_svconn_getctx(System.IntPtr conn);
        [DllImport (DllName)]
        private static extern unsafe void mtk_svconn_finish_login(ulong login_cid, ulong cid, uint msgid, byte *data, uint datalen);
        [DllImport (DllName)]
        private static extern unsafe ulong mtk_svconn_defer_login(System.IntPtr conn);
        [DllImport (DllName)]
        private static extern unsafe System.IntPtr mtk_svconn_find_deferred(ulong lcid);


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
        [DllImport (DllName)]
        private static extern unsafe System.IntPtr mtk_cid_getctx(ulong cid);

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
        private static extern unsafe void mtk_conn_timeout(System.IntPtr conn, ulong duration);
        [DllImport (DllName)]
        private static extern unsafe ulong mtk_conn_reconnect_wait(System.IntPtr conn); 
        [DllImport (DllName)]
        private static extern unsafe void mtk_conn_watch(System.IntPtr conn, Closure clsr);
        [DllImport (DllName)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern unsafe bool mtk_conn_connected(System.IntPtr conn);


        //wrapper
        public interface IConn {
            ulong Id { get; }
            void Close();
        }
        public interface IContextSetter {
            T SetContext<T>(T obj);
        }
        public partial interface ISVConn : IConn {
            uint Msgid { get; }
            void Reply(uint msgid, byte[] data);
            void Task(uint type, byte[] data);
            void Throw(uint msgid, byte[] data);
            void Notify(uint type, byte[] data);
            T Context<T>();
        }
        public interface IServerLogic {
            ulong OnAccept(ulong cid, IContextSetter setter, byte[] data, out byte[] rep);
            int OnRecv(ISVConn c, int type, byte[] data);
            void OnClose(ulong cid);
            void Poll();
            void Shutdown();
        }
        public partial class Conn : IConn {
            public unsafe delegate void RPCResultCB(int type, byte *bytes, uint len);
            System.IntPtr conn_;
            ClientRecvCB on_recv_;
            List<GCHandle> cbmems_;
            internal Conn(System.IntPtr c, List<GCHandle> cbmems) {
                conn_ = c;
                cbmems_ = cbmems;
                unsafe {
                    on_recv_ = RecvCB;
                }
                cbmems_.Add(GCHandle.Alloc(on_recv_));
            }
            public void Destroy() {
                unsafe {
                    if (conn_ != System.IntPtr.Zero) {
                        mtk_conn_close(conn_);
                    }
                }
                foreach (var gch in cbmems_) {
                    gch.Free();
                }
            }
            public ulong Id { 
                get { unsafe { return mtk_conn_cid(conn_); } } 
            }
            public bool IsConnected {
                get { unsafe { return mtk_conn_connected(conn_); } }
            }
            public ulong ReconnectWait {
                get { unsafe { return mtk_conn_reconnect_wait(conn_); } }
            }
            [MonoPInvokeCallback(typeof(Core.ClientReadyCB))]
            static unsafe void RecvCB(System.IntPtr arg, int type, byte *bytes, uint len) {
                GCHandle gch = GCHandle.FromIntPtr(arg);
                ((RPCResultCB)gch.Target)(type, bytes, len);
                gch.Free();
            }
            public void Send(uint type, byte[] data, RPCResultCB on_recv, ulong timeout_duration = 0) {
                var gch = GCHandle.Alloc(on_recv);
                unsafe {
                    fixed (byte* d = data) { 
                        mtk_conn_send(conn_, type, d, (uint)data.Length, new Closure {
                            arg = GCHandle.ToIntPtr(gch),
                            cb = Marshal.GetFunctionPointerForDelegate(on_recv_),
                        }); 
                    }
                }
            }
            public void Timeout(ulong duration) {
                unsafe { 
                    mtk_conn_timeout(conn_, duration);
                }
            }
            public void Watch(Closure clsr) {
                unsafe { mtk_conn_watch(conn_, clsr); }
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
        public partial class SVConn : ISVConn, IContextSetter {
            System.IntPtr conn_;
            public SVConn(System.IntPtr c) {
                conn_ = c;
            }
            public ulong Id {
                get { unsafe { return mtk_svconn_cid(conn_); } }
            }
            public uint Msgid {
                get { unsafe { return mtk_svconn_msgid(conn_); } }
            }
            public void Reply(uint msgid, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_svconn_send(conn_, msgid, d, (uint)data.Length); } }
            }
            public void Task(uint type, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_svconn_task(conn_, type, d, (uint)data.Length); } }
            }
            public void Throw(uint msgid, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_svconn_error(conn_, msgid, d, (uint)data.Length); } }
            }
            public void Notify(uint type, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_svconn_notify(conn_, type, d, (uint)data.Length); } }
            }
            public void Close() {
                unsafe { mtk_svconn_close(conn_); }
            }
            [MonoPInvokeCallback(typeof(Core.DestroyPointerCB))]
            static void DestroyContext(System.IntPtr ptr) {
                GCHandle.FromIntPtr(ptr).Free();
            }
            public T SetContext<T>(T obj) {
                var ptr = GCHandle.Alloc(obj);
                unsafe {
                    mtk_svconn_putctx(conn_, GCHandle.ToIntPtr(ptr), DestroyContext);
                }
                return (T)ptr.Target;                
            }
            public T Context<T>() {
                unsafe {
                    System.IntPtr ptr = mtk_svconn_getctx(conn_);
                    if (ptr == System.IntPtr.Zero) {
                        return default(T);
                    } else {
                        return (T)GCHandle.FromIntPtr(ptr).Target;
                    }
                }
            }
            static public void Reply(ulong cid, uint msgid, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_cid_send(cid, msgid, d, (uint)data.Length); } }
            }
            static public void Throw(ulong cid, uint msgid, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_cid_error(cid, msgid, d, (uint)data.Length); } }
            }
            static public void Notify(ulong cid, uint type, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_cid_notify(cid, type, d, (uint)data.Length); } }
            }
            static public ulong IdFromPtr(System.IntPtr c) {
                unsafe { return mtk_svconn_cid(c); }
            }

        }
        public partial class CidConn : ISVConn {
            ulong cid_;
            uint msgid_;
            public CidConn(ulong cid, uint msgid) {
                cid_ = cid;
                msgid_ = msgid;
            }
            public ulong Id {
                get { unsafe { return cid_; } }
            }
            public uint Msgid {
                get { unsafe { return msgid_; } }
            }
            public void Reply(uint msgid, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_cid_send(cid_, msgid, d, (uint)data.Length); } }
            }
            public void Task(uint type, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_cid_task(cid_, type, d, (uint)data.Length); } }
            }
            public void Throw(uint msgid, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_cid_error(cid_, msgid, d, (uint)data.Length); } }
            }
            public void Notify(uint type, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_cid_notify(cid_, type, d, (uint)data.Length); } }
            }
            public void Close() {
                unsafe { mtk_cid_close(cid_); }
            }
            public T Context<T>() {
                unsafe {
                    System.IntPtr ptr = mtk_cid_getctx(cid_);
                    if (ptr == System.IntPtr.Zero) {
                        return default(T);
                    } else {
                        return (T)GCHandle.FromIntPtr(ptr).Target;
                    }
                }
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
            public bool Initialized {
                get { return server_ != System.IntPtr.Zero && queue_ != System.IntPtr.Zero; }
            }
            public void Destroy() {
                unsafe {
                    if (server_ != System.IntPtr.Zero) {
                        mtk_server_join(server_);
                    }
                }
            }
            public unsafe void Process(IServerLogic logic) {
                logic.Poll();
                System.IntPtr elem = new System.IntPtr();
                while (mtk_queue_pop(queue_, ref elem)) {
                    ServerEvent *ev = (ServerEvent *)elem;
                    if (ev->lcid != 0) {
                        System.IntPtr dc = mtk_svconn_find_deferred(ev->lcid);
                        if (dc != System.IntPtr.Zero) {
                            var ret = new byte[ev->datalen];
                            byte[] rep;
                            byte *bp = (((byte *)ev) + SERVER_EVENT_TRUE_SIZE);
                            Marshal.Copy((System.IntPtr)bp, ret, 0, (int)ev->datalen);
                            var cid = logic.OnAccept(ev->cid, new SVConn(dc), ret, out rep);
                            fixed (byte *pb = rep) {
                                mtk_svconn_finish_login(ev->lcid, cid, ev->msgid, pb, (uint)rep.Length);
                            }
                        }
                    } else if (ev->msgid != 0) {
                        var c = new CidConn(ev->cid, ev->msgid);
                        var ret = new byte[ev->datalen];
                        Marshal.Copy((System.IntPtr)(((byte *)ev) + SERVER_EVENT_TRUE_SIZE), ret, 0, (int)ev->datalen);
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
            static unsafe public Address MakeAddress(byte* host, byte* cert, byte* key, byte* ca) {
                return new Address { 
                    host = host[0] == 0 ? System.IntPtr.Zero : (System.IntPtr)host, 
                    cert = cert[0] == 0 ? System.IntPtr.Zero : (System.IntPtr)cert, 
                    key = key[0] == 0 ? System.IntPtr.Zero : (System.IntPtr)key, 
                    ca = ca[0] == 0 ? System.IntPtr.Zero : (System.IntPtr)ca };
            }
        }
        public class ClientBuilder : Builder {
            ulong id_;
            byte[] payload_;
            Closure on_connect_, on_close_, on_ready_, on_start_, on_notify_;
            List<GCHandle> cbmems_ = new List<GCHandle>();
            public ClientBuilder() {}
            public ClientBuilder ConnectTo(string at) {
                base.ListenAt(at);
                return this;
            }
            public new ClientBuilder Certs(string cert, string key, string ca) {
                base.Certs(cert, key, ca);
                return this;            
            }
            public ClientBuilder Credential(ulong id, byte[] data) {
                id_ = id;
                payload_ = data;
                return this;
            }
            //these function will be called from same thread as Unity's main thread
            public ClientBuilder OnClose(ClientCloseCB cb, System.IntPtr arg) {
                cbmems_.Add(GCHandle.Alloc(cb));
                on_close_.arg = arg;
                on_close_.cb = Marshal.GetFunctionPointerForDelegate(cb);
                return this;
            }
            public ClientBuilder OnConnect(ClientConnectCB cb, System.IntPtr arg) {
                cbmems_.Add(GCHandle.Alloc(cb));
                on_connect_.arg = arg;
                on_connect_.cb = Marshal.GetFunctionPointerForDelegate(cb);
                return this;
            }
            public ClientBuilder OnReady(ClientReadyCB cb, System.IntPtr arg) {
                cbmems_.Add(GCHandle.Alloc(cb));
                on_ready_.arg = arg;
                on_ready_.cb = Marshal.GetFunctionPointerForDelegate(cb);
                return this;
            }
            public ClientBuilder OnNotify(ClientRecvCB cb, System.IntPtr arg) {
                cbmems_.Add(GCHandle.Alloc(cb));
                on_notify_.arg = arg;
                on_notify_.cb = Marshal.GetFunctionPointerForDelegate(cb);
                return this;
            }
            public ClientBuilder OnStart(ClientStartCB cb, System.IntPtr arg) {
                cbmems_.Add(GCHandle.Alloc(cb));
                on_start_.arg = arg;
                on_start_.cb = Marshal.GetFunctionPointerForDelegate(cb);
                return this;                
            }
            public Conn Build() {
                var b_host = System.Text.Encoding.UTF8.GetBytes(host_ + "\0");
                var b_cert = System.Text.Encoding.UTF8.GetBytes(cert_ + "\0");
                var b_key = System.Text.Encoding.UTF8.GetBytes(key_ + "\0");
                var b_ca = System.Text.Encoding.UTF8.GetBytes(ca_ + "\0");
                unsafe {
                    fixed (byte* h = b_host, c = b_cert, k = b_key, a = b_ca, p = payload_) {
                        Address addr = MakeAddress(h, c, k, a);
                        ClientConfig conf = new ClientConfig { 
                            on_connect = on_connect_, on_close = on_close_, 
                            on_start = on_start_, on_ready = on_ready_,
                        };
                        var conn = new Conn(mtk_connect(ref addr, ref conf), cbmems_);
                        Core.Instance().ConnMap[id_] = conn;
                        conn.Watch(on_notify_);
                        return conn;
                    }
                }
            }
        }
        public class ServerBuilder : Builder {
            uint n_worker_;
            bool use_queue_ = true;
            //TODO: support closuer mode
            //Closure handler_, acceptor_, closer_;
            public ServerBuilder() {
            }
            public new ServerBuilder ListenAt(string at) {
                base.ListenAt(at);
                return this;
            }
            public new ServerBuilder Certs(string cert, string key, string ca) {
                base.Certs(cert, key, ca);
                return this;
            }
            public ServerBuilder Worker(uint n_worker) {
                n_worker_ = n_worker;
                return this;
            }
            public ServerBuilder UseQueue(bool b) {
                use_queue_ = b;
                return this;
            }
            //TODO: allow non use queue mode
#if !MTKSV
            public Server Build() {
                System.IntPtr svp = BuildRaw();
                var s = new Server(svp);
                if (s.Initialized) {
                    Core.Instance().ServerMap[host_] = s;
                    return s;
                } else {
                    return null;
                }
            }
            protected System.IntPtr BuildRaw() {
#else
            public System.IntPtr Build() {
#endif
                var b_host = System.Text.Encoding.UTF8.GetBytes(host_ + "\0");
                var b_cert = System.Text.Encoding.UTF8.GetBytes(cert_ + "\0");
                var b_key = System.Text.Encoding.UTF8.GetBytes(key_ + "\0");
                var b_ca = System.Text.Encoding.UTF8.GetBytes(ca_ + "\0");
                unsafe {
                    fixed (byte* h = b_host, c = b_cert, k = b_key, a = b_ca) {
                        Address addr; ServerConfig conf;
                        addr = MakeAddress(h, c, k, a);
                        conf = new ServerConfig { 
                            n_worker = n_worker_, exclusive = false,
                            use_queue = use_queue_,
                            //handler = handler_, acceptor = acceptor_, closer = closer_, 
                        };                
#if !MTKSV
                        System.IntPtr svp = System.IntPtr.Zero;
                        mtk_listen(ref addr, ref conf, ref svp);
                        return svp;
#else
                        return mtkdn_server(ref addr, ref conf);
#endif
                    }
                }
            }
        }

        static Core instance_ = null;
        public Dictionary<ulong, Conn> ConnMap { get; set; }
        public Dictionary<string, Server> ServerMap { get; set; }
        static public ulong Tick { 
            get { return mtk_time(); } 
        }
        static public uint Time {
            get { return (uint)mtk_second(); }
        }
        static public void Sleep(ulong d) {
            mtk_pause(d);
        }
        static public ulong Sec2Tick(uint sec) { return ((ulong)sec) * 1000 * 1000 * 1000; }
        static public ulong FSec2Tick(float sec) { return ((ulong)(sec * 1000f * 1000f)) * 1000; }
        static public ulong MSec2Tick(uint msec) { return ((ulong)msec) * 1000 * 1000; }
        static public ulong USec2Tick(uint usec) { return ((ulong)usec) * 1000; }
        static public ulong NSec2Tick(uint nsec) { return ((ulong)nsec); }
        static public uint Tick2Sec(ulong tick) { return (uint)(tick / (1000 * 1000 * 1000)); }
        static public Core Instance() {
            if (instance_ == null) {
                instance_ = new Core();
            }
            return instance_;
        }
        public static void InitLogger(string name, LogWriteCB writer) {
            mtk_log_config(name, writer);
        }
        public static void Log(LogLevel lv, string str) {
#if UNITY_EDITOR
            switch (lv) {
            case Core.LogLevel.Trace:
            case Core.LogLevel.Debug:
                UnityEngine.Debug.Log(str);
                break;
            case Core.LogLevel.Info:
            case Core.LogLevel.Report:
                UnityEngine.Debug.Log(str + ",lv_:" + lv);
                break;
            case Core.LogLevel.Warn:
                UnityEngine.Debug.LogWarning(str + ",lv_:" + lv);
                break;
            case Core.LogLevel.Error:
            case Core.LogLevel.Fatal:
                UnityEngine.Debug.LogError(str + ",lv_:" + lv);
                break;
            }
#else
            mtk_log((int)lv, str);
#endif
        }
        public static void Assert(bool expr) {
            if (!expr) {
                var st = new  System.Diagnostics.StackTrace(1, true);
                Log(LogLevel.Fatal, "assertion fails at " + st.ToString());
                //TODO: real abortion
            }
        }
        public static void PutSlice(System.IntPtr slice, byte[] bytes) {
            unsafe {
                fixed(byte *b = bytes) {
                    mtk_slice_put(slice, b, (uint)bytes.Length);
                }
            }
        }
        public static void Ref() { unsafe { mtk_lib_ref(); } }
        public static void Unref() { unsafe { mtk_lib_unref(); } }
        Core() {
            ConnMap = new Dictionary<ulong, Conn>();
            ServerMap = new Dictionary<string, Server>();
        }
    }
    public class Log {
        public static void Write(Core.LogLevel lv, string str) {
            Core.Log(lv, str);
        }
        public static void Info(string str) {
            Core.Log(Core.LogLevel.Info, str);
        }
        public static void Error(string str) {
            Core.Log(Core.LogLevel.Error, str);
        }
        public static void Debug(string str) {
            Core.Log(Core.LogLevel.Debug, str);
        }
        public static void Fatal(string str) {
            Core.Log(Core.LogLevel.Fatal, str);
        }
        public static void Report(string str) {
            Core.Log(Core.LogLevel.Report, str);
        }
    }
#if MTKSV
    public class EntryPointBase {
        static protected Core.IServerLogic logic_;
        static public void SetLogic(Core.IServerLogic l) {
            logic_ = l;
        }
        static public unsafe ulong Login(System.IntPtr c, ulong cid, byte* data, uint len, out byte[] repdata) {
            var ret = new byte[len];
            Marshal.Copy((System.IntPtr)data, ret, 0, (int)len);
            return logic_.OnAccept(cid, new Core.SVConn(c), ret, out repdata);
        }
        static public unsafe bool Handle(System.IntPtr c, int type, byte* data, uint len) {
            var ret = new byte[len];
            Marshal.Copy((System.IntPtr)data, ret, 0, (int)len);
            return logic_.OnRecv(new Core.SVConn(c), type, ret) >= 0;
        }
        static public void Close(System.IntPtr c) {
            logic_.OnClose(Core.SVConn.IdFromPtr(c));
        }
    }
#endif
}





