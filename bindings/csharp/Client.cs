using System.Collections.Generic;
using System.Threading;
#if !MTK_DISABLE_ASYNC
using System.Threading.Tasks;
#endif
using System.Runtime.InteropServices;
using Google.Protobuf;
#if !MTKSV
using AOT;
#endif

namespace Mtk {
    public partial class Core {
        public partial class Conn {
            public unsafe delegate void RPCResultCB(int type, byte *bytes, uint len);
            System.IntPtr conn_;
            ClientRecvCB on_recv_;
            List<GCHandle> cbmems_;
            internal Conn() {}
            internal Conn(System.IntPtr c, List<GCHandle> cbmems) {
                Init(c, cbmems);
            }
            internal void Init(System.IntPtr c, List<GCHandle> cbmems) {
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
#if !MTKSV
            [MonoPInvokeCallback(typeof(Core.ClientReadyCB))]
#endif
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
        }

        public class Builder {
            protected class Listener {
                public string host_, cert_, key_, ca_;
                public GCHandle gchost_, gccert_, gckey_, gcca_;
            }
            protected List<Listener> listeners_ = new List<Listener>();
            public Builder() {}
            public Builder ListenAt(string host, string cert = "", string key = "", string ca = "") {
                listeners_.Add(new Listener {
                    host_ = host,
                    cert_ = cert,
                    key_ = key,
                    ca_ = ca,
                });
                return this;
            }
            public Address[] MakeAddress() {
                Address[] addrs = new Address[listeners_.Count];
                for (int i = 0; i < listeners_.Count; i++) {
                    var l = listeners_[i];
                    var b_host = System.Text.Encoding.UTF8.GetBytes(l.host_ + "\0");
                    var b_cert = System.Text.Encoding.UTF8.GetBytes(l.cert_ + "\0");
                    var b_key = System.Text.Encoding.UTF8.GetBytes(l.key_ + "\0");
                    var b_ca = System.Text.Encoding.UTF8.GetBytes(l.ca_ + "\0");
                    l.gchost_ = GCHandle.Alloc(b_host, GCHandleType.Pinned);
                    l.gccert_ = GCHandle.Alloc(b_cert, GCHandleType.Pinned);
                    l.gckey_ = GCHandle.Alloc(b_key, GCHandleType.Pinned);
                    l.gcca_ = GCHandle.Alloc(b_ca, GCHandleType.Pinned);
                    addrs[i] = new Address { 
                        host = b_host[0] == 0 ? System.IntPtr.Zero : l.gchost_.AddrOfPinnedObject(), 
                        cert = b_cert[0] == 0 ? System.IntPtr.Zero : l.gccert_.AddrOfPinnedObject(), 
                        key = b_key[0] == 0 ? System.IntPtr.Zero : l.gckey_.AddrOfPinnedObject(), 
                        ca = b_ca[0] == 0 ? System.IntPtr.Zero : l.gcca_.AddrOfPinnedObject() 
                    };
                }
                return addrs;
            }
            public void DestroyAddress() {
                for (int i = 0; i < listeners_.Count; i++) {
                    var l = listeners_[i];
                    l.gchost_.Free();
                    l.gccert_.Free();
                    l.gckey_.Free();
                    l.gcca_.Free();
                }
            }
        }
        public sealed class ClientBuilder : Builder {
        	Client client_;
        	bool auto_cleanup_ = true;
            Closure on_connect_, on_close_, on_ready_, on_start_, on_notify_;
            List<GCHandle> cbmems_ = new List<GCHandle>();
            public ClientBuilder() {}
            public ClientBuilder ConnectTo(string at, string cert = "", string key = "", string ca = "") {
            	string resolved;
            	if (Util.NAT.Instance == null || !Util.NAT.Instance.Resolve(at, out resolved)) {
            		resolved = at;
            	}
            	Mtk.Log.Info("clientbuilder: connect to:" + at + " => " + resolved);
                base.ListenAt(resolved, cert, key, ca);
                return this;            
            }
            //for building client these function will be called from same thread as Unity's main thread
            public ClientBuilder Handler(Client.IEventHandler h) {
            	client_ = new Client(h);
            	return this;
            }
            public ClientBuilder RegisterNotifier<N>(uint t, Client.Notifier<N> notifier) where N : IMessage, new() {
            	client_.RegisterNotifier<N>(t, notifier);
            	return this;
            }
            public ClientBuilder AutoCleanup(bool yes_or_no) {
            	auto_cleanup_ = yes_or_no;
            	return this;
            }
            //for building conn. these function will be called from same thread as Unity's main thread
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
            ClientConfig MakeConfig() {
            	return new ClientConfig { 
                    on_connect = on_connect_, on_close = on_close_, 
                    on_start = on_start_, on_ready = on_ready_,
                };
            }
            public Client Build() {
            	if (client_ == null) {
            		Mtk.Log.Error("do not use OnXXXX to create Client. use Handler instead");
            		return null;
            	}            	
            	var c = client_;
            	var ptr = c.CallbacksPtr;
                unsafe {
    				OnConnect(Client.OnConnect, ptr)
    					.OnClose(Client.OnClose, ptr)
    					.OnReady(Client.OnReady, ptr)
    					.OnStart(Client.OnStart, ptr)
    					.OnNotify(Client.OnNotify, ptr);

                    Address[] addrs = MakeAddress();
                    ClientConfig conf = MakeConfig();
    				c.Start(addrs[0], conf, cbmems_, auto_cleanup_);
    				c.Watch(on_notify_);
    				DestroyAddress();
                }
				return c;				    	
            }
            public Conn BuildConn() {
            	if (client_ != null) {
            		Mtk.Log.Error("cannot use handler to build conn");
            		return null;
            	}
                Address[] addrs = MakeAddress();
                ClientConfig conf = MakeConfig();
                var conn = new Conn(mtk_connect(ref addrs[0], ref conf), cbmems_);
                conn.Watch(on_notify_);
                DestroyAddress();
                return conn;
            }
        }
        public class Client : Conn {
			public unsafe delegate void NotifyReceiver(byte* bytes, uint len);
            public class SendResult<REP> {
                public REP Reply;
                public IError Error;
            }
			public interface IEventHandler {
				ulong OnStart(out byte[] payload);
				bool OnConnect(ulong cid, byte[] payload);
				ulong OnClose(ulong cid, int connect_attempts);
				bool OnReady();
			};
			class CallbackCollection {
				public Dictionary<uint, NotifyReceiver> Notifiers { get; set; }
				public IEventHandler Handler { get; set; }
			}

        	internal System.IntPtr CallbacksPtr { get; set; }
        	public Client(IEventHandler handler) : base() {
				var cc = GCHandle.Alloc(new CallbackCollection {
					Handler = handler,
					Notifiers = new Dictionary<uint, NotifyReceiver>(),
				});
				CallbacksPtr = GCHandle.ToIntPtr(cc);        		
			}
			internal void Start(Address addr, ClientConfig conf, List<GCHandle> cbmems, bool auto_cleanup) {
				base.Init(mtk_connect(ref addr, ref conf), cbmems);
#if UNITY_EDITOR
				if (auto_cleanup) {
                    var self = this;
					Unity.ExitHandler.Instance().AtExit(Unity.ExitHandler.Priority.Client, delegate () { self.Stop(); });
				}
#endif
			}
			public void Stop() {
				base.Destroy();
				if (CallbacksPtr != System.IntPtr.Zero) {
					GCHandle.FromIntPtr(CallbacksPtr).Free();
				}
			}

			public delegate void Receiver<REP, ERR>(REP rep, ERR err);
			public void Send<REP,ERR>(uint t, IMessage req, Receiver<REP,ERR> cb) 
				where REP : IMessage, new()
				where ERR : IMessage, new() {
				byte[] payload;
				if (Mtk.Codec.Pack(req, out payload) < 0) {
					return;
				}
				unsafe {
					Send((uint)t, payload, delegate (int type, byte *bytes, uint len) {
						if (type >= 0) {
							REP rep = new REP();
							if (Mtk.Codec.Unpack(bytes, len, ref rep) < 0) {
								return;
							}
							cb(rep, default(ERR));
						} else {
							ERR err = new ERR();
							if (Mtk.Codec.Unpack(bytes, len, ref err) < 0) {
								return;
							}
							cb(default(REP), err);
						}
					});
				}
			}
#if !MTK_DISABLE_ASYNC
            public async Task<SendResult<REP>> Send<REP, ERR>(uint t, Google.Protobuf.IMessage req)
                where REP : Google.Protobuf.IMessage, new()
                where ERR : IError, new() {
                var tcs = new TaskCompletionSource<SendResult<REP>>();
                Send<REP,ERR>(t, req, delegate (REP rep, ERR err) {
                    if (rep != null) {
                        tcs.TrySetResult(new SendResult<REP>{Reply = rep});
                    } else {
                        tcs.TrySetResult(new SendResult<REP>{Error = err});
                    }
                });
                return await tcs.Task.ConfigureAwait(false);
            }
#endif
			public delegate void Notifier<N>(N n);
			public void RegisterNotifier<N>(uint t, Notifier<N> notifier) where N : IMessage, new() {
				unsafe {
					ToCallbacks(CallbacksPtr).Notifiers[t] = delegate (byte *bytes, uint len) {
						N payload = new N();
						if (Mtk.Codec.Unpack(bytes, len, ref payload) < 0) {
							return;
						}
						notifier(payload);
					};
				}
			}

			//callbacks
			static CallbackCollection ToCallbacks(System.IntPtr arg) {
				return (CallbackCollection)GCHandle.FromIntPtr(arg).Target;
			}
#if !MTKSV
			[MonoPInvokeCallback(typeof(Core.ClientConnectCB))]
#endif
			static internal unsafe bool OnConnect(System.IntPtr arg, ulong cid, byte *bytes, uint len) {
				byte[] arr = new byte[len];
				Marshal.Copy((System.IntPtr)bytes, arr, 0, (int)len);
				ToCallbacks(arg).Handler.OnConnect(cid, arr);
				return true;
			}
#if !MTKSV
			[MonoPInvokeCallback(typeof(Core.ClientCloseCB))]
#endif
			static internal ulong OnClose(System.IntPtr arg, ulong cid, int connect_attempts) {
				return ToCallbacks(arg).Handler.OnClose(cid, connect_attempts);
			}
#if !MTKSV
			[MonoPInvokeCallback(typeof(Core.ClientReadyCB))]
#endif
			static internal bool OnReady(System.IntPtr arg) {
				return ToCallbacks(arg).Handler.OnReady();
			}
#if !MTKSV
			[MonoPInvokeCallback(typeof(Core.ClientStartCB))]
#endif
			static internal ulong OnStart(System.IntPtr arg, System.IntPtr slice) {
				byte[] payload;
				var cid = ToCallbacks(arg).Handler.OnStart(out payload);
				Mtk.Core.PutSlice(slice, payload);
				return cid;
			}
#if !MTKSV
			[MonoPInvokeCallback(typeof(Core.ClientRecvCB))]
#endif
			static internal unsafe void OnNotify(System.IntPtr arg, int type, byte *bytes, uint len) {
				NotifyReceiver n;
				if (ToCallbacks(arg).Notifiers.TryGetValue((uint)type, out n)) {
					n(bytes, len);
				} else {
					Mtk.Log.Error("notify not handled:" + type);
				}
			}
        }
	}
}