using System.Collections.Generic;
using YamlDotNet.Core;
using YamlDotNet.Serialization;

namespace Mtk.Unity {
struct Cluster {
    public struct Deploy {
    	public int replicas;
    	public string mode;
    }
    public struct Port {
    	public string mode { get; set; }
    	public int target { get; set; }
    	public int published { get; set; }
		public class Converter : IYamlTypeConverter {
			public bool Accepts(System.Type type) {
			    return type == typeof(Port);
			}
			public object ReadYaml(IParser parser, System.Type type) {
				Port port = new Port();
				int target, published;
				if (parser.Current is YamlDotNet.Core.Events.Scalar) {
				    var scalar = (YamlDotNet.Core.Events.Scalar)parser.Current;
				    //UnityEngine.Debug.Log("scalar value:" + scalar.Value);
				    if (int.TryParse(scalar.Value.Split(':')[0], out published) && 
				    	int.TryParse(scalar.Value.Split(':')[1], out target)) {
					    port.published = published;
					    port.target = target;
					    port.mode = "normal";
					}
				    parser.MoveNext();
				} else if (parser.Current is YamlDotNet.Core.Events.MappingStart) {
					string key = null;
				    parser.MoveNext();
					while (parser.Current is YamlDotNet.Core.Events.Scalar) {
					    var scalar = (YamlDotNet.Core.Events.Scalar)parser.Current;
					    //UnityEngine.Debug.Log("scalar value:" + scalar.Value);
						if (key == null) {
							key = scalar.Value;
						} else if (key == "mode") {
							port.mode = scalar.Value;
							key = null;
						} else if (key == "published" && int.TryParse(scalar.Value, out published)) {
							port.published = published;
							key = null;
						} else if (key == "target" && int.TryParse(scalar.Value, out target)) {
							port.target = target;
							key = null;
						}
						parser.MoveNext();
					}
					parser.MoveNext();
				} 
				return port;
			}
			public void WriteYaml(IEmitter emitter, object value, System.Type type) {
			    var port = (Port)value;
			}
		}
    }
   	public struct Service {
   		public string image { get; set; }
   		public List<Port> ports { get; set; }
   		public List<string> environment { get; set; }
   		public Deploy deploy { get; set; }

   		public string Logic {
   			get {
   				return FindEnv("MTKSV_LOGIC");
   			}
   		}
   		public int Port(int idx) {
   			if (ports != null && idx < ports.Count) {
   				return ports[idx].published;
   			} else {
   				return 0;
   			}
   		}

   		public string FindEnv(string key) {
			if (environment == null) {
				return null;
			}
			foreach (var e in environment) {
				if (e.Substring(0, key.Length) == key) {					
					return e.Substring(key.Length + 1);
				}
			}
			return null;
   		}
   	};

    public string version { get; set; }
   	public Dictionary<string, Service> services { get; set; }
}
}
