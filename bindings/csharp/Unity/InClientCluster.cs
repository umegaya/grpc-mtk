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
				if (s.Value.Logic == null) {
					Debug.Log("service " + s.Key + " is not for emurating in Unity Editor");
					continue;
				}
				GameObject go = new GameObject(s.Key);
				go.transform.parent = gameObject.transform;
				var cs = go.AddComponent(typeof(Mtk.Unity.InClientServer)) as Mtk.Unity.InClientServer;
				cs.service_name_ = s.Key;
				cs.logic_class_name_ = s.Value.Logic;
				cs.args_ = s.Value.Args;
				cs.Bootstrap();
			}
		}
	}
}
