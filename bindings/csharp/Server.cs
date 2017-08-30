using System.Runtime.InteropServices;
using System.Threading;
#if !MTKSV
using AOT;
#endif
#if !MTK_DISABLE_ASYNC
using System.Threading.Tasks;
#endif

namespace Mtk {
    public partial class Core {

    	//interface
        public partial interface ISVConn {
            ulong Id { get; }
            uint Msgid { get; }
            void Close();
            void Reply(uint msgid, byte[] data);
            void Task(uint type, byte[] data);
            void Throw(uint msgid, byte[] data);
            void Notify(uint type, byte[] data);
            T Context<T>();
        }
        public partial interface IContextSetter {
            T SetContext<T>(T obj);
        }

        //logic class need to have static methods
        /*
        static ServerBuilder Bootstrap(string[]);
        static IServerLogic Instance();
        (but no way to force this)
        */
        public abstract partial class IServerLogic {
            public abstract void OnAccept(ulong cid, IContextSetter setter, byte[] data);
            public abstract int OnRecv(ISVConn c, int type, byte[] data);
            public abstract void OnClose(ulong cid);
            public abstract void Poll();
            public abstract void Shutdown();

            System.IntPtr server_;
            [System.ThreadStatic]
            static internal Core.ManualPumpingSynchronizationContext sync_;
            static public Core.ManualPumpingSynchronizationContext SyncCtx { get { return sync_; } }
            public CidConn NewConn(ulong cid, uint msgid) {
                return new CidConn(cid, msgid, server_);
            }
            internal void SetServerDescriptor(System.IntPtr sv) {
                server_  = sv;
            }
            internal void TlsInit() {
                sync_ = new Core.ManualPumpingSynchronizationContext();
            }
            internal void TlsFin() {}
            
            protected HandleResult Ok(Google.Protobuf.IMessage reply) {
                return new HandleResult{ Reply = reply, Error = null };
            }
            protected HandleResult Error(IError error) {
                return new HandleResult{ Reply = null, Error = error };
            }
            protected AcceptResult Accept(ulong cid, Google.Protobuf.IMessage reply) {
                return new AcceptResult{ Cid = cid, Reply = reply, Error = null };
            }
            protected AcceptResult Reject(IError error) {
                return new AcceptResult{ Reply = null, Error = error };
            }
        }

        //classes
        public partial class DeferredSVConn : IContextSetter {
            ulong lcid_;
            uint msgid_;
            public DeferredSVConn(ulong lcid, uint msgid) {
                lcid_ = lcid;
                msgid_ = msgid;
            }
            public T SetContext<T>(T obj) {
                var conn = mtk_svconn_find_deferred(lcid_);
                if (conn != System.IntPtr.Zero) {
                    return SVConn.CreateContext<T>(conn, obj);
                } else {
                    Mtk.Log.Error("ev:deferred conn already closed,lcid:" + lcid_);
                    return default(T);
                }
            }
            public void FinishLogin(ulong cid, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_svconn_finish_login(lcid_, cid, msgid_, d, (uint)data.Length); } }
            }
            static internal ulong DeferLogin(System.IntPtr c) {
                unsafe { return mtk_svconn_defer_login(c); }
            }
            static internal uint GetDeferMsgid(System.IntPtr c) {
                unsafe { return mtk_svconn_msgid(c); }
            }
        }
        public partial class SVConn : ISVConn {
            internal System.IntPtr conn_;
            internal SVConn(System.IntPtr c) {
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
            public T SetContext<T>(T obj) {
                return CreateContext<T>(conn_, obj);             
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

            #if !MTKSV
            [MonoPInvokeCallback(typeof(Core.DestroyPointerCB))]
            #endif
            static void DestroyContext(System.IntPtr ptr) {
                Mtk.Log.Info("ev:DestroyContext");
                GCHandle.FromIntPtr(ptr).Free();
            }
            static public T CreateContext<T>(System.IntPtr conn, T obj) {
                var ptr = GCHandle.Alloc(obj);
                unsafe {
                    mtk_svconn_putctx(conn, GCHandle.ToIntPtr(ptr), DestroyContext);
                }
                return (T)ptr.Target;                
            }
            static public ulong IdFromPtr(System.IntPtr c) {
                unsafe { return mtk_svconn_cid(c); }
            }
        }
        public partial class CidConn : ISVConn {
            ulong cid_;
            uint msgid_;
            System.IntPtr sv_;
            public CidConn(ulong cid, uint msgid, System.IntPtr sv) {
                cid_ = cid;
                msgid_ = msgid;
                sv_ = sv;
            }
            public ulong Id {
                get { unsafe { return cid_; } }
            }
            public uint Msgid {
                get { unsafe { return msgid_; } }
            }
            public void Reply(uint msgid, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_cid_send(sv_, cid_, msgid, d, (uint)data.Length); } }
            }
            public void Task(uint type, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_cid_task(sv_, cid_, type, d, (uint)data.Length); } }
            }
            public void Throw(uint msgid, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_cid_error(sv_, cid_, msgid, d, (uint)data.Length); } }
            }
            public void Notify(uint type, byte[] data) {
                unsafe { fixed (byte* d = data) { mtk_cid_notify(sv_, cid_, type, d, (uint)data.Length); } }
            }
            public void Close() {
                unsafe { mtk_cid_close(sv_, cid_); }
            }
            public T Context<T>() {
                unsafe {
                    System.IntPtr ptr = mtk_cid_getctx(sv_, cid_);
                    if (ptr == System.IntPtr.Zero) {
                        return default(T);
                    } else {
                        return (T)GCHandle.FromIntPtr(ptr).Target;
                    }
                }
            }
        }
        public sealed class ServerBuilder : Builder {
            uint n_worker_;
            bool use_queue_ = true;
            static string service_name_;
            static internal void SetCurrentServiceName(string name) {
            	service_name_ = name;
            } 
            //TODO: support closuer mode
            //Closure handler_, acceptor_, closer_;
            public ServerBuilder() {
            }
            public new ServerBuilder ListenAt(string at, string cert = "", string key = "", string ca = "") {
            	string resolved;
            	if (Util.NAT.Instance == null || !Util.NAT.Instance.Translate(service_name_, at, out resolved)) {
            		resolved = at;
            	}
            	Mtk.Log.Info("ServerBuilder: listen at:" + at + "=>" + resolved);
                base.ListenAt(resolved, cert, key, ca);
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
            internal Server Build() {
#else
            internal System.IntPtr Build() {
#endif
                Address[] addr = MakeAddress();
                ServerConfig conf = new ServerConfig { 
                    n_worker = n_worker_, exclusive = false,
                    use_queue = use_queue_,
                    //handler = handler_, acceptor = acceptor_, closer = closer_, 
                };                
#if !MTKSV
                System.IntPtr svp = System.IntPtr.Zero;
                mtk_listen(addr, addr.Length, ref conf, ref svp);
                var s = new Server(svp);
                if (s.Initialized) {
                    DestroyAddress();
                    return s;
                } else {
                    DestroyAddress();
                    return null;
                }
#else
                DestroyAddress();
                return mtkdn_server(addr, addr.Length, ref conf);
#endif
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
            public System.IntPtr Descriptor {
                get { return server_; }
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
                        var ret = new byte[ev->datalen];
                        byte *bp = (((byte *)ev) + SERVER_EVENT_TRUE_SIZE);
                        Marshal.Copy((System.IntPtr)bp, ret, 0, (int)ev->datalen);
                        logic.OnAccept(ev->cid, new DeferredSVConn(ev->lcid, ev->msgid), ret);
                    } else if (ev->msgid != 0) {
                        var c = new CidConn(ev->cid, ev->msgid, server_);
                        var ret = new byte[ev->datalen];
                        Marshal.Copy((System.IntPtr)(((byte *)ev) + SERVER_EVENT_TRUE_SIZE), ret, 0, (int)ev->datalen);
                        logic.OnRecv(c, ev->result, ret);
                    } else {
                        logic.OnClose(ev->cid);
                    }
                    mtk_queue_elem_free(queue_, elem);
                }
                IServerLogic.SyncCtx.Update(); //resume pending continuation (awaited tasks)
            }
            //typical accept and recv handlers (sync)
            public delegate AcceptResult AcceptHandler<REQ, REP, ERR>(ulong cid, Core.IContextSetter setter, REQ req);
            static public void OnAccept<REQ, REP, ERR>(ulong cid, Core.IContextSetter setter, byte[] data, AcceptHandler<REQ, REP, ERR> hd) 
                where REQ : Google.Protobuf.IMessage, new() 
                where REP : Google.Protobuf.IMessage, new()
                where ERR : IError, new() {
                var req = new REQ();
                IError err = null;
                AcceptResult res = null;
                byte[] repdata;
                if (Codec.Unpack(data, ref req) >= 0) {
                    try {
                        res = hd(cid, setter, req);
                        err = res.Error;
                        cid = res.Cid;
                    } catch (System.Exception e) {
                        err = new ERR();
                        err.Set(e);
                    }
                    if (err == null) {
                        if (Codec.Pack(res.Reply, out repdata) >= 0) {
                            (setter as DeferredSVConn).FinishLogin(cid, repdata);
                            return;
                        }
                        err = new ERR();
                        err.Set(Core.SystemErrorCode.PayloadPackFail);
                    }
                } else {
                    err = new ERR();
                    err.Set(Core.SystemErrorCode.PayloadUnpackFail); 
                }
                Codec.Pack(err, out repdata);
                Mtk.Log.Error("ev:OnAccept fails,msg:" + err.Message);
                if (Codec.Pack(err, out repdata) >= 0) {
                    (setter as DeferredSVConn).FinishLogin(0, repdata);
                } else {
                    (setter as DeferredSVConn).FinishLogin(0, new byte[0]);
                }
                return;
            }

            public delegate HandleResult Handler<REQ, REP, ERR>(Core.ISVConn c, REQ req);
            static public int Handle<REQ, REP, ERR>(Core.ISVConn c, byte[] data, Handler<REQ, REP, ERR> hd) 
                where REQ : Google.Protobuf.IMessage, new() 
                where REP : Google.Protobuf.IMessage, new()
                where ERR : IError, new() {
                //Mtk.Log.Info("Handle received:" + typeof(REQ));
                var req = new REQ();
                HandleResult res = null;
                if (Codec.Unpack(data, ref req) >= 0) {
                    IError err = null;
                    try {
                        res = hd(c, req);
                        err = res.Error;
                    } catch (System.Exception e) {
                        err = new ERR();
                        err.Set(e);
                    }
                    if (err == null) {
                        if (!c.Reply(c.Msgid, res.Reply)) {
                            err = new ERR();
                            err.Set(Core.SystemErrorCode.PayloadPackFail);
                            c.Throw(c.Msgid, err);
                        }
                    } else if (!err.Pending) {
                        Mtk.Log.Error("ev:handler fail,emsg:" + err.Message);
                        c.Throw(c.Msgid, err);
                    }
                    return 0;
                } 
                Mtk.Log.Error("ev:invalid request payload,id:" + c.Id);
                return -1;
            }

#if !MTK_DISABLE_ASYNC
            //typical accept and recv handlers (async)
            public delegate Task<AcceptResult> AsyncAcceptHandler<REQ, REP>(ulong cid, Core.IContextSetter setter, REQ req);
            static public async void OnAcceptAsync<REQ, REP, ERR>(ulong cid, Core.IContextSetter setter, byte[] data, AsyncAcceptHandler<REQ, REP> hd) 
                where REQ : Google.Protobuf.IMessage, new() 
                where REP : Google.Protobuf.IMessage, new()
                where ERR : IError, new() {
                var req = new REQ();
                AcceptResult res = null;
                IError err;
                byte[] repdata;
                if (Codec.Unpack(data, ref req) >= 0) {
                    try {
                        var pctx =  SynchronizationContext.Current;
                        try {
                            SynchronizationContext.SetSynchronizationContext(Mtk.Core.IServerLogic.SyncCtx);
                            res = await hd(cid, setter, req).ConfigureAwait(false);
                            err = res.Error;
                        } finally {
                            SynchronizationContext.SetSynchronizationContext(pctx);                                
                        }
                    } catch (System.Exception e) {
                        err = new ERR();
                        err.Set(e);
                    }
                    if (err == null) {
                        if (Codec.Pack(res.Reply, out repdata) >= 0) {
                            (setter as DeferredSVConn).FinishLogin(res.Cid, repdata);
                            return;
                        }
                        err = new ERR();
                        err.Set(Core.SystemErrorCode.PayloadPackFail);
                    }
                } else {
                    err = new ERR();
                    err.Set(Core.SystemErrorCode.PayloadUnpackFail); 
                }
                Mtk.Log.Error("ev:OnAccept fails,msg:" + err.Message);
                if (Codec.Pack(err, out repdata) >= 0) {
                    (setter as DeferredSVConn).FinishLogin(0, repdata);
                } else {
                    (setter as DeferredSVConn).FinishLogin(0, new byte[0]);
                }
            }

            public delegate Task<HandleResult> AsyncHandler<REQ, REP>(Core.ISVConn c, REQ req);
            static public async void HandleAsync<REQ, REP, ERR>(Core.ISVConn c, byte[] data, AsyncHandler<REQ, REP> hd) 
                where REQ : Google.Protobuf.IMessage, new() 
                where REP : Google.Protobuf.IMessage, new()
                where ERR : IError, new() {
                //Mtk.Log.Info("Handle received:" + typeof(REQ));
                var req = new REQ();
                if (Codec.Unpack(data, ref req) >= 0) {
                    var msgid = c.Msgid; //stored current msgid to local variable because blockin at await below may change msgid 
                    HandleResult res = null;
                    IError err;
                    try {
                        var pctx = SynchronizationContext.Current;
                        try {
                            SynchronizationContext.SetSynchronizationContext(Mtk.Core.IServerLogic.SyncCtx);
                            res = await hd(c, req).ConfigureAwait(false);
                            err = res.Error;
                        } finally {
                            SynchronizationContext.SetSynchronizationContext(pctx);                                
                        }
                    } catch (System.Exception e) {
                        err = new ERR();
                        err.Set(e);
                    }
                    if (err == null) {
                        if (!(res.Reply is REP) || !c.Reply(msgid, res.Reply)) {
                            err = new ERR();
                            err.Set(Core.SystemErrorCode.PayloadPackFail);
                            c.Throw(msgid, err);
                        }
                    } else if (!err.Pending) {
                        Mtk.Log.Error("ev:handler fail,emsg:" + err.Message);
                        c.Throw(msgid, err);
                    }
                    return;
                } 
                Mtk.Log.Error("ev:invalid request payload,id:" + c.Id);
                return;
            }
#endif // !MTK_DISABLE_ASYNC
        }
    }
#if MTKSV
    public class EntryPoint {
        static public void Initialize(Core.IServerLogic logic, System.IntPtr svdesc) {
            logic.SetServerDescriptor(svdesc);
        }
        static public void Shutdown(Core.IServerLogic logic) {
            logic.Shutdown();
        }
        static public unsafe ulong Login(Core.IServerLogic logic, 
                                    System.IntPtr c, ulong cid, byte* data, uint len) {
            var ret = new byte[len];
            Marshal.Copy((System.IntPtr)data, ret, 0, (int)len);
            ulong lcid = Core.DeferredSVConn.DeferLogin(c);
            uint msgid = Core.DeferredSVConn.GetDeferMsgid(c);
            logic.OnAccept(cid, new Core.DeferredSVConn(lcid, msgid), ret);
            return 0;
        }
        static public unsafe bool Handle(Core.IServerLogic logic, 
                                    System.IntPtr c, int type, byte* data, uint len) {
            var ret = new byte[len];
            Marshal.Copy((System.IntPtr)data, ret, 0, (int)len);
            return logic.OnRecv(new Core.SVConn(c), type, ret) >= 0;
        }
        static public void Close(Core.IServerLogic logic, System.IntPtr c) {
            logic.OnClose(Core.SVConn.IdFromPtr(c));
        }
        static public void TlsInit(Core.IServerLogic logic) {
            logic.TlsInit();
        }
        static public void TlsFin(Core.IServerLogic logic) {
            logic.TlsFin();
        }
        static public void Poll(Core.IServerLogic logic) {
            logic.Poll();
            Core.IServerLogic.SyncCtx.Update(); //execute pending continuation (awaited tasks)
        }
    }
#endif
}
