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
		public string[] args_;
		public void Bootstrap() {
			if (sv_ == null) {
				Core.ServerBuilder.SetCurrentServiceName(service_name_);
				var t = Util.GetType(logic_class_name_);
				var bootstrap = t.GetMethod("Bootstrap");
				if (bootstrap == null) {
					Debug.LogError("Logic class need to implement static method 'Bootstrap'");
					return;
				}
				sv_ = (bootstrap.Invoke(null, new object[]{args_}) as Core.ServerBuilder).Build();
			}
		}
		protected void Start() {
			var t = Util.GetType(logic_class_name_);
			var instance = t.GetMethod("Instance");
			logic_ = instance.Invoke(null, null) as Core.IServerLogic;
#if UNITY_EDITOR
			var self = this;
			ExitHandler.Instance().AtExit(ExitHandler.Priority.Server, delegate () { self.Stop(); } );
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
