using System.Collections;
using System.Collections.Generic;
using Google.Protobuf;
using System.Runtime.InteropServices;
using AOT;

namespace Mtk {
public class Conn {
	public unsafe delegate void NotifyReceiver(byte* bytes, uint len);
	public interface IEventHandler {
		ulong OnStart(out byte[] payload);
		bool OnConnect(ulong cid, byte[] payload);
		ulong OnClose(ulong cid, int connect_attempts);
		bool OnReady();
	};
	class CallbackCollection {
		public ulong Id { get; set; }
		public Dictionary<uint, NotifyReceiver> Notifiers { get; set; }
		public IEventHandler Handler { get; set; }
	}	

	Mtk.Core.Conn conn_;
	string connectTo_, cert_, key_, ca_;
	System.IntPtr callbacks_;
	public ulong Id { get { return ToCallbacks(callbacks_).Id; } }

	public Conn(string connectTo, ulong cid, IEventHandler handler, 
				string cert = null, string key = null, string ca = null) {
		connectTo_ = connectTo;
		cert_ = cert;
		key_ = key;
		ca_ = ca;
		var cc = GCHandle.Alloc(new CallbackCollection {
			Id = cid,
			Handler = handler,
			Notifiers = new Dictionary<uint, NotifyReceiver>(),
		});
		callbacks_ = GCHandle.ToIntPtr(cc);
	}
	public void Start() {
		unsafe {
			conn_ = (new Mtk.Core.ClientBuilder())
				.ConnectTo(connectTo_)
				.Certs(cert_, key_, ca_) 
				.OnConnect(OnConnect, callbacks_)
				.OnClose(OnClose, callbacks_)
				.OnReady(OnReady, callbacks_)
				.OnStart(OnStart, callbacks_)
				.OnNotify(OnNotify, callbacks_)
				.Build();
		}
	}
	public void Stop() {
		if (conn_ != null) {
			conn_.Destroy();
		}
		if (callbacks_ != System.IntPtr.Zero) {
			GCHandle.FromIntPtr(callbacks_).Free();
		}
	}
	public void Poll() {
		conn_.Poll();
	}
	public void SetTimeout(uint duration_sec) {
		conn_.Timeout(Mtk.Core.Sec2Tick(duration_sec));
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
			conn_.Send((uint)t, payload, delegate (int type, byte *bytes, uint len) {
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
	public delegate void Notifier<N>(N n);
	public void RegisterNotifier<N>(uint t, Notifier<N> notifier) where N : IMessage, new() {
		unsafe {
			ToCallbacks(callbacks_).Notifiers[t] = delegate (byte *bytes, uint len) {
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
	[MonoPInvokeCallback(typeof(Core.ClientConnectCB))]
	static protected unsafe bool OnConnect(System.IntPtr arg, ulong cid, byte *bytes, uint len) {
		byte[] arr = new byte[len];
		Marshal.Copy((System.IntPtr)bytes, arr, 0, (int)len);
		ToCallbacks(arg).Id = cid;
		ToCallbacks(arg).Handler.OnConnect(cid, arr);
		return true;
	}
	[MonoPInvokeCallback(typeof(Core.ClientCloseCB))]
	static protected ulong OnClose(System.IntPtr arg, ulong cid, int connect_attempts) {
		return ToCallbacks(arg).Handler.OnClose(cid, connect_attempts);
	}
	[MonoPInvokeCallback(typeof(Core.ClientReadyCB))]
	static protected bool OnReady(System.IntPtr arg) {
		return ToCallbacks(arg).Handler.OnReady();
	}
	[MonoPInvokeCallback(typeof(Core.ClientStartCB))]
	static protected ulong OnStart(System.IntPtr arg, System.IntPtr slice) {
		byte[] payload;
		var cid = ToCallbacks(arg).Handler.OnStart(out payload);
		Mtk.Core.PutSlice(slice, payload);
		return cid;
	}
	[MonoPInvokeCallback(typeof(Core.ClientRecvCB))]
	static protected unsafe void OnNotify(System.IntPtr arg, int type, byte *bytes, uint len) {
		NotifyReceiver n;
		if (ToCallbacks(arg).Notifiers.TryGetValue((uint)type, out n)) {
			n(bytes, len);
		} else {
			Mtk.Log.Error("notify not handled:" + type);
		}
	}
}
}
