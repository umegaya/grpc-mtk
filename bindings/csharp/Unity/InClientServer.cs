using System.Collections.Generic;

namespace Mtk {
	public class InClientServer : MonoBehavior {
		Server sv_;
		IServerLogic logic_;
		public string listenAt_ = "0.0.0.0:50051";
		public int worker_ = 1;
		void Start() {
			logic_ = ServerLogic();
			if (logic_ == null) {
				UnityEngine.Debug.Assert(False);
			}
			sv_ = (new ServerBuilder())
				.ListenAt(listenAt_)
				.Worker(worker_)
				.Build();
		}
		void FixedUpdate() {
			unsafe {
				sv_.Process(logic_);
			}
		}
		virtual IServerLogic ServerLogic() { return null; }
	}
}
