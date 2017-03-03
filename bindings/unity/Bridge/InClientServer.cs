using System.Collections.Generic;

namespace Mtk {
	public class InClientServer : MonoBehavior {
		Server sv_;
		IServerLogic logic_;
		public string listenAt_ = "localhost:50051";
		public int reader_ = 1;
		public int writer_ = 1;
		void Start() {
			InitServerLogic();
			sv_ = (new ServerBuilder())
				.ListenAt(listenAt_)
				.Worker(reader_, writer_)
				.Build();
		}
		void FixedUpdate() {
			unsafe {
				System.IntPtr elem;
				while (sv_.PopEvent(ref elem)) {
					ServerEvent *ev = (ServerEvent *)elem;
					var svcn = new CidConn(ev->cid, ev->msgid);
					if (ev->lcid != 0) {
						var ret = new byte[ev->datalen];
						Marshal.Copy(((uint8_t *)ev) + sizeof(ServerEvent), ret, 0, ev->datalen);
						var rep = OnAccept(svcn, ev->cid, ret);
						if (rep.cid != 0) {
							mtk_svconn_finish_login(ev->lcid, rep.cid, ev->msgid, rep.data, rep.data.Length);
						}
					} else {
						var ret = new byte[ev->datalen];
						Marshal.Copy(((uint8_t *)ev) + sizeof(ServerEvent), ret, 0, ev->datalen);
						OnRecv(svcn, ev->cid, ret);
					}
					sv_.FreeEvent(elem);
				}
			}
		}
		virtual void InitServerLogic() {}
		Server.AcceptReply OnAccept(SVConn c, ulong cid, byte[] cred) {
			return logic_.OnAccept(c, cid, cred);
		}
		int OnRecv(SVConn c, uint type, byte[] data) {
			return logic_.OnRecv(c, type, data);
		}
	}
}
