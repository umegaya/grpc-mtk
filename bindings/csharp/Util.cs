using System.Reflection;
using System.Collections.Generic;
using YamlDotNet.Core;
using YamlDotNet.Serialization;

namespace Mtk {
    public partial class Core {
		public static void PutSlice(System.IntPtr slice, byte[] bytes) {
            unsafe {
                fixed(byte *b = bytes) {
                    mtk_slice_put(slice, b, (uint)bytes.Length);
                }
            }
        }
	}
	public class Util {
		static public System.Type GetType( string TypeName ) {
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
		public struct ComposeFile {
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
		   	static Deserializer NewDeserializer() {
		   		return new DeserializerBuilder().
					WithTypeConverter(new ComposeFile.Port.Converter()).
					IgnoreUnmatchedProperties().
					Build();
		   	}
		   	static public ComposeFile Load(System.IO.TextReader reader) {
				return NewDeserializer().Deserialize<ComposeFile>(reader);
		   	}
		   	static public ComposeFile Load(string text) {
				return NewDeserializer().Deserialize<ComposeFile>(text);
		   	}
		}
		public class NAT {
			static NAT instance_ = null;
			static internal NAT Instance { get { return instance_; } }
			static internal void Initialize(ComposeFile cf) {
				instance_ = new NAT(cf);
			}

			struct Entry {
				public List<string> Addresses;
				public int RoundRobinIndex;
			}
			Dictionary<string, Entry> Table { get; set; }
			ComposeFile Compose { get; set; }
			int port_num_seed_ = 50150;

			NAT(ComposeFile cf) {
				Table = new Dictionary<string, Entry>();
				Compose = cf;
			}
			public bool Resolve(string service_and_port, out string resolved_host_and_port) {
				Entry ent;
				if (Table.TryGetValue(service_and_port, out ent)) {
					var idx = ent.RoundRobinIndex % ent.Addresses.Count;
					resolved_host_and_port = ent.Addresses[idx];
					ent.RoundRobinIndex = ((idx + 1) % ent.Addresses.Count);
					return true;
				}
				resolved_host_and_port = "";
				return false;
			}
			public bool Translate(string service_name, string host_and_port, out string translated_host_and_port) {
				if (Compose.services.ContainsKey(service_name)) {
					translated_host_and_port = "0.0.0.0:" + port_num_seed_++;
					Register(service_name + ":" + host_and_port.Split(':')[1], translated_host_and_port.Replace("0.0.0.0", "localhost"));
					return true;
				}
				translated_host_and_port = "";
				return false;
			}
			public void Register(string service_and_port, string host_and_port) {
				Entry ent;
				if (!Table.TryGetValue(service_and_port, out ent)) {
					ent = new Entry {
						Addresses = new List<string>(),
						RoundRobinIndex = 0,
					};
					Table[service_and_port] = ent;
				}
				ent.Addresses.Add(host_and_port);
			}
			public bool HasEntry(string service_and_port) {
				return Table.ContainsKey(service_and_port);
			}
		}
	}
}
