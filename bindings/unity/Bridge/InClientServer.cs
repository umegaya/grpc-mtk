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
