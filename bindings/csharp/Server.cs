using System.Runtime.InteropServices;
#if !MTKSV
using AOT;
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
        public interface IContextSetter {
            T SetContext<T>(T obj);
        }
        public interface IServerLogic {
            ulong OnAccept(ulong cid, IContextSetter setter, byte[] data, out byte[] rep);
            int OnRecv(ISVConn c, int type, byte[] data);
            void OnClose(ulong cid);
            void Poll();
            void Shutdown();
        }
        //logic class need to have static methods
        /*
        static ServerBuilder Bootstrap(string[]);
        static IServerLogic Instance();
        (but no way to force this)
        */


        //classes
        public partial class SVConn : ISVConn, IContextSetter {
            System.IntPtr conn_;
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
            #if !MTKSV
            [MonoPInvokeCallback(typeof(Core.DestroyPointerCB))]
            #endif
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
            public Server Build() {
#else
            public System.IntPtr Build() {
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
            //typical accept and recv handlers
            public delegate ERR AcceptHandler<REQ, REP, ERR>(ref ulong cid, Core.IContextSetter setter, REQ req, ref REP rep);
            static public ulong OnAccept<REQ, REP, ERR>(ulong cid, Core.IContextSetter setter, byte[] data, out byte[] repdata, AcceptHandler<REQ, REP, ERR> hd) 
                where REQ : Google.Protobuf.IMessage, new() 
                where REP : Google.Protobuf.IMessage, new()
                where ERR : Google.Protobuf.IMessage, IError, new() {
                var req = new REQ();
                ERR err = default(ERR);
                int ret = Codec.Unpack(data, ref req);
                if (ret >= 0) {
                    var rep = new REP();
                    try {
                        err = hd(ref cid, setter, req, ref rep);
                    } catch (System.Exception e) {
                        err = new ERR();
                        err.Set(e);
                    }
                    if (err == null) {
                        ret = Codec.Pack(rep, out repdata);
                        if (ret >= 0) {
                            return cid;
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
                return 0;
            }

            public delegate ERR Handler<REQ, REP, ERR>(Core.ISVConn c, REQ req, ref REP rep);
            static public int Handle<REQ, REP, ERR>(Core.ISVConn c, byte[] data, Handler<REQ, REP, ERR> hd) 
                where REQ : Google.Protobuf.IMessage, new() 
                where REP : Google.Protobuf.IMessage, new()
                where ERR : Google.Protobuf.IMessage, IError, new() {
                //Mtk.Log.Info("Handle received:" + typeof(REQ));
                var req = new REQ();
                if (Codec.Unpack(data, ref req) >= 0) {
                    REP rep = new REP();
                    ERR err = default(ERR);
                    try {
                        err = hd(c, req, ref rep);
                    } catch (System.Exception e) {
                        err = new ERR();
                        err.Set(e);
                    }
                    if (err == null) {
                        if (!c.Reply(c.Msgid, rep)) {
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
        }
    }
#if MTKSV
    public class EntryPoint {
        static public void Shutdown(Core.IServerLogic logic) {
            logic.Shutdown();
        }
        static public unsafe ulong Login(Core.IServerLogic logic, 
                                    System.IntPtr c, ulong cid, byte* data, uint len, out byte[] repdata) {
            var ret = new byte[len];
            Marshal.Copy((System.IntPtr)data, ret, 0, (int)len);
            return logic.OnAccept(cid, new Core.SVConn(c), ret, out repdata);
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
    }
#endif
}