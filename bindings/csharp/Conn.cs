using System.Collections;
using System.Collections.Generic;
using Google.Protobuf;
using System.Runtime.InteropServices;

namespace Mtk {
public class Conn {
	public unsafe delegate void NotifyReceiver(byte* bytes, uint len);
	public interface IEventHandler {
		ulong OnStart(out byte[] payload);
		bool OnConnect(ulong cid, byte[] payload);
		ulong OnClose(ulong cid, int connect_attempts);
		bool OnReady();
	};

	Mtk.Core.Conn conn_;
	string connectTo_, cert_, key_, ca_;
	IEventHandler handler_;
	ulong cid_;	
	Dictionary<uint, NotifyReceiver> Notifiers { get; set; }
	public ulong Id { get { return cid_; } }

	public Conn(string connectTo, ulong cid, IEventHandler handler, 
				string cert = null, string key = null, string ca = null) {
		connectTo_ = connectTo;
		cid_ = cid;
		handler_ = handler;
		cert_ = cert;
		key_ = key;
		ca_ = ca;
		Notifiers = new Dictionary<uint, NotifyReceiver>();
	}
	public void Start() {
		unsafe {
			conn_ = (new Mtk.Core.ClientBuilder())
				.ConnectTo(connectTo_)
				.Certs(cert_, key_, ca_) 
				.OnConnect(OnConnect)
				.OnClose(OnClose)
				.OnReady(OnReady)
				.OnStart(OnStart)
				.OnNotify(OnNotify)
				.Build();
		}
	}
	public void Stop() {
		if (conn_ != null) {
			conn_.Destroy();
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
			conn_.Send((uint)t, payload, delegate (System.IntPtr arg, int type, byte *bytes, uint len) {
				conn_.Finish((uint)arg);
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
			Notifiers[t] = delegate (byte *bytes, uint len) {
				N payload = new N();
				if (Mtk.Codec.Unpack(bytes, len, ref payload) < 0) {
					return;
				}
				notifier(payload);
			};
		}
	}

	//callbacks
	protected unsafe bool OnConnect(System.IntPtr arg, ulong cid, byte *bytes, uint len) {
		byte[] arr = new byte[len];
		Marshal.Copy((System.IntPtr)bytes, arr, 0, (int)len);
		cid_ = cid;
		handler_.OnConnect(cid, arr);
		return true;
	}
	protected ulong OnClose(System.IntPtr arg, ulong cid, int connect_attempts) {
		return handler_.OnClose(cid, connect_attempts);
	}
	protected bool OnReady(System.IntPtr arg) {
		return handler_.OnReady();
	}
	protected ulong OnStart(System.IntPtr arg, System.IntPtr slice) {
		byte[] payload;
		var cid = handler_.OnStart(out payload);
		Mtk.Core.PutSlice(slice, payload);
		return cid;
	}
	protected unsafe void OnNotify(System.IntPtr arg, int type, byte *bytes, uint len) {
		NotifyReceiver n;
		if (Notifiers.TryGetValue((uint)type, out n)) {
			n(bytes, len);
		} else {
			Mtk.Log.Error("notify not handled:" + type);
		}
	}
}
}
