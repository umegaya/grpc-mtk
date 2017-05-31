using System.Reflection;

namespace Mtk {
	class Util {
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
	}
}
