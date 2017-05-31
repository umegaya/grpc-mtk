using UnityEngine;
using System.Collections.Generic;
using YamlDotNet.Serialization;

namespace Mtk.Unity {
	public class InClientCluster : MonoBehaviour {
		public TextAsset composerFile_;
		public TextAsset cert_, key_, ca_;
		Cluster cluster_;
		ClusterNAT clusterNat_ = new ClusterNAT();

		protected void Start() {
			using(var reader = new System.IO.StringReader(composerFile_.text)) {
				var deser = new DeserializerBuilder().
								WithTypeConverter(new Cluster.Port.Converter()).
								IgnoreUnmatchedProperties().
								Build();
            	cluster_ = deser.Deserialize<Cluster>(reader);
 			}
 			CreateClusterFromSetting();
		}

		protected void CreateClusterFromSetting() {
			foreach (var s in cluster_.services) {
				Debug.Log("service:" + s.Key + "|" + s.Value.Runner + "|" + s.Value.Port(0) + "|" + s.Value.deploy.mode + "|" + s.Value.deploy.replicas);
				/*GameObject go = new GameObject(s.Key);
				go.transform.parent = gameObject.transform;
				if (s.Value.Runner == null) {
					Debug.LogError("service " + s.Key + " is not for emurating in Unity Editor");
					continue;
				}
				var cs = go.AddComponent(Util.GetType(s.Value.Runner)) as Mtk.Unity.InClientServer;
				if (cs == null) {
					Debug.LogError("fatal: cannot load server logic class:" + s.Value.Runner);
					return;
				}
				for (int i = 0; i < s.Value.PortNum; i++) {
					var port = s.Value.Port(i);
					if (clusterNat_.HasEntry(s.Key + ":" + port) {
						cs.listenAt_ = "0.0.0.0:" + port;
					} else {
						cs.listenAt_ = "0.0.0.0:0"; //make bind() choosing port 
					}
					cs.worker_ = s.Value.deploy.replicas;
					string bind_address = cs.Init();
					string service_address = s.Key + ":" + port;
					clusterNat_.Register(service_address, bind_address);
				}*/
			}
		}
	}
}
