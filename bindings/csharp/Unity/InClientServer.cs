using UnityEngine;
using System.Collections.Generic;

namespace Mtk.Unity {
	public class InClientServer : MonoBehaviour {
		//internal handles
		Core.Server sv_ = null;
		Core.IServerLogic logic_ = null;
		//exposed property
		public string service_name_;
		public string logic_class_name_;
		public List<string> args_;
		public void Init() {
			if (sv_ == null) {
				Core.ServerBuilder.SetCurrentServiceName(service_name_);
				var t = Util.GetType(logic_class_name_);
				var factory = t.GetMethod("Instance");
				logic_ = factory.Invoke(null, null) as Core.IServerLogic;
				sv_ = logic_.Bootstrap(args_.ToArray()).Build();
			}
		}
		protected void Start() {
			Init();
#if UNITY_EDITOR
			ExitHandler.Instance().AtExit(ExitHandler.Priority.Server, Stop);
#endif
		}
		protected void Stop() {
			//if server logic uses backend server connection, connection shutdown should be done before
			//server shutdown, because server need to be alive to do graceful shutdown
			logic_.Shutdown();
			if (sv_ != null) {
				sv_.Destroy();
			}
		}
		protected void FixedUpdate() {
			unsafe {
				if (sv_ != null) {
					sv_.Process(logic_);
				}
			}
		}
	}
}
