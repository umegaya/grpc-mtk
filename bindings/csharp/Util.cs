using System.Reflection;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading;
#if !MTK_DISABLE_ASYNC
using System.Threading.Tasks;
#endif
using Continuation = System.Collections.Generic.KeyValuePair<System.Threading.SendOrPostCallback, object>;

namespace Mtk {
    public partial class Core {
		public static void PutSlice(System.IntPtr slice, byte[] bytes) {
            unsafe {
                fixed(byte *b = bytes) {
                    mtk_slice_put(slice, b, (uint)bytes.Length);
                }
            }
        }

        //represents async rpc result
        public class HandleResult {
            public Google.Protobuf.IMessage Reply;
            public IError Error;
        }
        public class AcceptResult {
            public ulong Cid;
            public Google.Protobuf.IMessage Reply;
            public IError Error;
        }

        //synchronization context to 
        public class ManualPumpingSynchronizationContext : SynchronizationContext
		{
			Queue<Continuation> conts_ = new Queue<Continuation>();

			public override void Post(SendOrPostCallback d, object state)
			{
				conts_.Enqueue(new Continuation(d, state));
			}

			public void Update()
			{
				Continuation cont;
				while(true)
				{
					try {
						cont = conts_.Dequeue();
						cont.Key(cont.Value);
					} catch (System.InvalidOperationException) {
						break;
					}
				}
			}
		}

	}
	public partial class Util {
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
		public partial struct ComposeFile {
		    public struct Deploy {
		    	public int replicas;
		    	public string mode;
		    }
		    public partial struct Port {
		    	public string mode { get; set; }
		    	public int target { get; set; }
		    	public int published { get; set; }
		    }
		   	public struct Service {
		   		public string image { get; set; }
		   		public List<Port> ports { get; set; }
		   		public List<string> environment { get; set; }
		   		public string command { get; set; }
		   		public Deploy deploy { get; set; }

		   		public string Logic {
		   			get {
		   				return FindEnv("MTKSV_LOGIC");
		   			}
		   		}
		   		public string[] Args {
		   			get {
		   				return command.Split(default(string[]), System.StringSplitOptions.RemoveEmptyEntries);
		   			}
		   		}
		   		public int Port(int idx) {
		   			if (ports != null && idx < ports.Count) {
		   				return ports[idx].published;
		   			} else {
		   				return 0;
		   			}
		   		}
		   		public bool HasPort(string port, out Port setting) {
		   			setting = default(Port);
		   			if (ports == null) {
		   				return false;
		   			}
		   			foreach (var p in ports) {
		   				if (p.target.ToString() == port) {
		   					setting = p;
		   					return true;
		   				}
		   			}
		   			return false;
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
		   	}

		    public string version { get; set; }
		   	public Dictionary<string, Service> services { get; set; }
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
				if (Table.TryGetValue(service_and_port.Trim(), out ent)) {
					var idx = ent.RoundRobinIndex % ent.Addresses.Count;
					resolved_host_and_port = ent.Addresses[idx];
					ent.RoundRobinIndex = ((idx + 1) % ent.Addresses.Count);
					return true;
				}
				resolved_host_and_port = "";
				return false;
			}
			public bool Translate(string service_name, string host_and_port, out string translated_host_and_port) {
				var port = host_and_port.Split(':')[1];
				var key = service_name.Trim();
				if (Compose.services.ContainsKey(key)) {
					//if port is published, no address translation because its accessed from external executable
					ComposeFile.Port p;
					if (!Compose.services[key].HasPort(port, out p)) { 
						translated_host_and_port = "0.0.0.0:" + port_num_seed_++;
					} else {
						translated_host_and_port = "0.0.0.0:" + p.published;
					}
					Register(key + ":" + port, translated_host_and_port.Replace("0.0.0.0", "localhost"));
					return true;
				}
				translated_host_and_port = "";
				return false;
			}
			public void Register(string service_and_port, string host_and_port) {
				Entry ent;
				var key = service_and_port.Trim();
				if (!Table.TryGetValue(key, out ent)) {
					ent = new Entry {
						Addresses = new List<string>(),
						RoundRobinIndex = 0,
					};
					Table[key] = ent;
				}
				Mtk.Log.Info("ev:register to NAT,key:" + key + ",resolved:" + host_and_port);
				ent.Addresses.Add(host_and_port);
			}
			public bool HasEntry(string service_and_port) {
				return Table.ContainsKey(service_and_port.Trim());
			}
		}
	}
}
