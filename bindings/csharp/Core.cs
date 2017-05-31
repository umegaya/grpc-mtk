using System.Collections.Generic;
using System.Runtime.InteropServices;
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
        internal struct ServerConfig {
            public uint n_worker;
            public Closure handler, acceptor, closer;
            [MarshalAs(UnmanagedType.I1)] public bool exclusive; //if true, caller thread of mtk_listen blocks
            [MarshalAs(UnmanagedType.I1)] public bool use_queue;
        };
        internal struct ClientConfig {
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
        private static extern unsafe void mtk_listen(Address[] addrs, int n_addrs, ref ServerConfig conf, ref System.IntPtr sv);
        [DllImport (DllName)]
        private static extern unsafe System.IntPtr mtk_server_queue(System.IntPtr sv);
        [DllImport (DllName)]
        private static extern unsafe void mtk_server_join(System.IntPtr sv);
#if MTKSV
        [DllImport (DllName)]
        private static extern unsafe System.IntPtr mtkdn_server(Address[] addrs, int n_addrs, ref ServerConfig conf);
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


        //primitives
        static Core instance_ = null;
        static public Core Instance() {
            if (instance_ == null) {
                instance_ = new Core();
            }
            return instance_;
        }
        public static void Ref() { unsafe { mtk_lib_ref(); } }
        public static void Unref() { unsafe { mtk_lib_unref(); } }
        Core() {}
    }
}





