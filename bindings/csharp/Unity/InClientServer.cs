using UnityEngine;
using System.Collections.Generic;

namespace Mtk.Unity {
	public class InClientServer : MonoBehaviour {
		Core.Server sv_;
		Core.IServerLogic logic_;
		public string listenAt_ = "0.0.0.0:50051";
		public uint worker_ = 1;
		void Start() {
			logic_ = ServerLogic();
			if (logic_ == null) {
				UnityEngine.Debug.Assert(false);
			}
			sv_ = (new Core.ServerBuilder())
				.ListenAt(listenAt_)
				.Worker(worker_)
				.Build();
		}
		void FixedUpdate() {
			unsafe {
				sv_.Process(logic_);
			}
		}
		Core.IServerLogic ServerLogic() { return null; }
	}
}
