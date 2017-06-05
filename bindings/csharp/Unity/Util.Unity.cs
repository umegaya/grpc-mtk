using System.Reflection;
using System.Collections.Generic;
using YamlDotNet.Core;
using YamlDotNet.Serialization;

namespace Mtk {
	public partial class Util {
		public partial struct ComposeFile {
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

		   	public partial struct Port {
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
		}
	}
}
