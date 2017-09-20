using System;
using System.Collections.Generic;
using System.Reflection;
using System.Linq;
using System.Runtime.CompilerServices;
using Dapper;

namespace Mtk
{
	public partial class Database
    {
    	const string ColumnAttributeName = "ColumnAttribute";
        public static PropertyInfo SelectProperty(Type type, string columnName)
        {
            return
                type.GetProperties(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance).
                    FirstOrDefault(
                        prop =>
                        prop.GetCustomAttributes(false)
                            // Search properties to find the one ColumnAttribute applied with Name property set as columnName to be Mapped 
                            .Any(attr => attr.GetType().Name == ColumnAttributeName
                                         &&
                                         attr.GetType().GetProperties(BindingFlags.Public |
                                                                      BindingFlags.NonPublic |
                                                                      BindingFlags.Instance)
                                             .Any(
                                                 f =>
                                                 f.Name == "Name" &&
                                                 f.GetValue(attr).ToString().ToLower() == columnName.ToLower()))
                        && // Also ensure the property is not read-only
                        (prop.DeclaringType == type
                             ? prop.GetSetMethod(true)
                             : prop.DeclaringType.GetProperty(prop.Name,
                                                              BindingFlags.Public | BindingFlags.NonPublic |
                                                              BindingFlags.Instance).GetSetMethod(true)) != null
                    );
        }

        public static ColumnAttribute[] SelectColumns(Type type) {
        	List<ColumnAttribute> attrs = new List<ColumnAttribute>();
			foreach (var prop in type.GetProperties(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance)) {
				var a = prop.GetCustomAttributes(false).FirstOrDefault(attr => attr.GetType().Name == ColumnAttributeName) as ColumnAttribute;
				if (a != default(ColumnAttribute)) {
					attrs.Add(a);
				}
			}
			return attrs.ToArray();
        }
	    
		[AttributeUsage(AttributeTargets.Property, AllowMultiple = true)]
		public class ColumnAttribute : Attribute
		{
			public string Name { get; set; }
			public string PropName { get; set; }
#if !MTK_USE35
			public ColumnAttribute([CallerMemberName] string prop_name = null) { 
				this.Name = prop_name; this.PropName = prop_name; 
			}
#else
			public ColumnAttribute(string name) {
				this.Name = name; this.PropName = name;
			}
#endif
		}

		public static void TypeMap(Assembly assembly) {
			foreach(Type type in assembly.GetTypes()) {
		        if (type.GetCustomAttributes(typeof(TableAttribute), true).Length > 0) {
			        Dapper.SqlMapper.SetTypeMap(type, new CustomPropertyTypeMap(type, SelectProperty));	
		        }
		    }
		}

		[AttributeUsage(AttributeTargets.Class, AllowMultiple = true)]
		public class TableAttribute : Attribute
		{
			public string Name { get; set; }
#if !MTK_USE35
			public TableAttribute([CallerMemberName] string name = null) { 
				this.Name = name;
			}
#else
			public TableAttribute(string name) {
				this.Name = name;
			}
#endif
		}
	 
		public class FallbackTypeMapper : SqlMapper.ITypeMap
		{
			private readonly IEnumerable<SqlMapper.ITypeMap> _mappers;
	 
			public FallbackTypeMapper(IEnumerable<SqlMapper.ITypeMap> mappers)
			{
				_mappers = mappers;
			}
			public ConstructorInfo FindExplicitConstructor() {
				foreach (var mapper in _mappers)
				{
					try
					{
						ConstructorInfo result = mapper.FindExplicitConstructor();
						if (result != null)
						{
							return result;
						}
					}
					catch (NotImplementedException)
					{
					}
				}
				return null;				
			}
			public ConstructorInfo FindConstructor(string[] names, Type[] types)
			{
				foreach (var mapper in _mappers)
				{
					try
					{
						ConstructorInfo result = mapper.FindConstructor(names, types);
						if (result != null)
						{
							return result;
						}
					}
					catch (NotImplementedException)
					{
					}
				}
				return null;
			}
	 
			public SqlMapper.IMemberMap GetConstructorParameter(ConstructorInfo constructor, string columnName)
			{
				foreach (var mapper in _mappers)
				{
					try
					{
						var result = mapper.GetConstructorParameter(constructor, columnName);
						if (result != null)
						{
							return result;
						}
					}
					catch (NotImplementedException)
					{
					}
				}
				return null;
			}
	 
			public SqlMapper.IMemberMap GetMember(string columnName)
			{
				foreach (var mapper in _mappers)
				{
					try
					{
						var result = mapper.GetMember(columnName);
						if (result != null)
						{
							return result;
						}
					}
					catch (NotImplementedException)
					{
					}
				}
				return null;
			}
		}

		//register model
		public static void RegisterModel<T>() {
			Dapper.SqlMapper.SetTypeMap(typeof(T), new CustomPropertyTypeMap(typeof (T), SelectProperty));
		}
	}
}