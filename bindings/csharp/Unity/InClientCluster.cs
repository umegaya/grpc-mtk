using UnityEngine;
using System.Collections.Generic;

namespace Mtk.Unity {
	public class InClientCluster : MonoBehaviour {
		public TextAsset composeYaml_;
		public TextAsset cert_, key_, ca_;
		Util.ComposeFile compose_;

		protected void Start() {
			compose_ = Util.ComposeFile.Load(composeYaml_.text);
			Util.NAT.Initialize(compose_);
 			CreateClusterFromSetting();
		}

		protected void CreateClusterFromSetting() {
			foreach (var s in compose_.services) {
				Debug.Log("service:" + s.Key + "|" + s.Value.Logic + "|" + s.Value.Port(0) + "|" + s.Value.deploy.mode + "|" + s.Value.deploy.replicas);
				/*GameObject go = new GameObject(s.Key);
				go.transform.parent = gameObject.transform;
				if (s.Value.Runner == null) {
					Debug.LogError("service " + s.Key + " is not for emurating in Unity Editor");
					continue;
				}
				var cs = go.AddComponent(typeof(Mtk.Unity.InClientServer));
				if (cs == null) {
					Debug.LogError("fatal: cannot load server logic class:" + s.Value.Runner);
					return;
				}
				cs.service_name_ = s.Key;
				cs.logic_class_name_ = s.Value.Logic;
				cs.args_ = new string[] {}; */
			}
		}
	}
}
