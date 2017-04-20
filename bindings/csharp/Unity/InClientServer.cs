using UnityEngine;
using System.Collections.Generic;

namespace Mtk.Unity {
	public class InClientServer : MonoBehaviour {
		Core.Server sv_ = null;
		Core.IServerLogic logic_ = null;
		public string listenAt_ = "0.0.0.0:50051";
		public uint worker_ = 1;
		protected void Start() {
			logic_ = ServerLogic();
			if (logic_ == null) {
				UnityEngine.Debug.Assert(false);
				return;
			}
			sv_ = (new Core.ServerBuilder())
				.ListenAt(listenAt_)
				.Worker(worker_)
				.Build();
		}
		protected void Stop() {
			if (sv_ != null) {
				sv_.Finalize();
			}
			logic_.Shutdown();
		}
		protected void FixedUpdate() {
			unsafe {
				if (sv_ != null) {
					sv_.Process(logic_);
				}
			}
		}
		protected virtual Core.IServerLogic ServerLogic() { return null; }
	}
}
