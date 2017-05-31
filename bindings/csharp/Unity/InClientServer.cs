using UnityEngine;
using System.Collections.Generic;

namespace Mtk.Unity {
	public class InClientServer : MonoBehaviour {
		//typedef
		[System.Serializable]
		public struct Environment {
			public string key;
			public string val;
		};
		//internal handles
		Core.Server sv_ = null;
		Core.IServerLogic logic_ = null;
		//exposed property
		public string logic_class_name_;
		public List<string> args_;
		public List<Environment> env_;
		public string[] Init() {
			if (sv_ == null) {
				foreach (var kv in env_) {
					System.Environment.SetEnvironmentVariable(kv.key, kv.val);
				}
				var t = Util.GetType(logic_class_name_);
				var factory = t.GetMethod("Instance");
				logic_ = factory.Invoke(null, null) as Core.IServerLogic;
				sv_ = logic_.Bootstrap(args_.ToArray()).Build();
			}
			return sv_.AddrList;
		}
		protected void Start() {
			Init();
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
