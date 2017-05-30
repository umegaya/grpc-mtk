using UnityEngine;
using System.Collections.Generic;
using System.Reflection;
using YamlDotNet.Serialization;

namespace Mtk.Unity {
	public class InClientCluster : MonoBehaviour {
		public TextAsset composerFile_;
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
				var cs = go.AddComponent(GetType(s.Value.Runner)) as Mtk.Unity.InClientServer;
				if (cs == null) {
					Debug.LogError("fatal: cannot load server logic class:" + s.Value.Runner);
					return;
				}
				for (int i = 0; i < s.Value.PortNum; i++) {
					if (clusterNat_.HasEntry(s.Key + ":" + s.Value.Port(i))) {

					}
					cs.listenAt_ = "0.0.0.0:0"; //make bind() choosing port 
					cs.worker_ = s.Value.deploy.replicas;
				}*/
			}
		}


		static protected System.Type GetType( string TypeName ) {
			var type = System.Type.GetType( TypeName );
			// If it worked, then we're done here
			if( type != null ) {
				return type;
			}
			// If the TypeName is a full name, then we can try loading the defining assembly directly
			if( TypeName.Contains( "." ) ) {
				// Get the name of the assembly (Assumption is that we are using 
				// fully-qualified type names)
				var assemblyName = TypeName.Substring( 0, TypeName.IndexOf( '.' ) );
				// Attempt to load the indicated Assembly
				var assembly = Assembly.Load( assemblyName );
				if( assembly == null ) {
					return null;
				}
				// Ask that assembly to return the proper Type
				type = assembly.GetType( TypeName );
				if( type != null ) {
					return type;
				}
			}
			// If we still haven't found the proper type, we can enumerate all of the 
			// loaded assemblies and see if any of them define the type
			var currentAssembly = Assembly.GetExecutingAssembly();
			var referencedAssemblies = currentAssembly.GetReferencedAssemblies();
			foreach( var assemblyName in referencedAssemblies ) {
				// Load the referenced assembly
				var assembly = Assembly.Load( assemblyName );
				if( assembly != null ) {
					// See if that assembly defines the named type
					type = assembly.GetType( TypeName );
					if( type != null ) {
						return type;
					}
				}
			}
			// The type just couldn't be found...
			return null;
		}
	}
}
