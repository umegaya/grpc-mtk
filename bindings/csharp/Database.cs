using System.Data;
#if MTKSV
using MySql.Data;
#else
using Mono.Data.Sqlite;
#endif
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;
using System.Linq;
using Dapper;
using Dapper.Contrib.Extensions;
using SimpleMigrations;

namespace Mtk {
	public partial class Database {
		static public IDbConnection Open(string url) {
#if MTKSV
			var c = new MySqlConnection(url);
#else
			var c = new SqliteConnection(url);
#endif
			Migrate(c);
			return c;
		}
		static protected void Migrate(System.Data.Common.DbConnection c) {
			c.Open();
			var migrationsAssembly = typeof(Database).Assembly;
			IDatabaseProvider<System.Data.Common.DbConnection> prov;
#if MTKSV
			prov = new SimpleMigrations.DatabaseProvider.MysqlDatabaseProvider(c);
#else
			prov = new SimpleMigrations.DatabaseProvider.SqliteDatabaseProvider(c);
#endif
			Migration.BackendConnection = c;
			var mig = new SimpleMigrator(migrationsAssembly, prov);
			mig.Load();
			mig.MigrateToLatest();
		}
		public class Conn {
			public IDbConnection Raw;
			public IDbTransaction TxHandle;
			internal Conn(IDbConnection raw) {
				Raw = raw;
				TxHandle = raw.BeginTransaction();		
			}
			internal void Commit() { TxHandle.Commit(); }
			internal void Rollback() { TxHandle.Rollback(); }
			//caution: you specify correct table name by yourself
			public Task<int> ExecuteAsync(string sql, 
											object param = null, 
											IDbTransaction transaction = null, 
											int? commandTimeout = null, 
											CommandType? commandType = null) {
				return Raw.ExecuteAsync(sql, param, transaction ?? TxHandle, commandTimeout, commandType);
			}
			public Task<IEnumerable<T>> SelectAllAsync<T>(string where_clause, 
														object param = null, 
														IDbTransaction transaction = null, 
														int? commandTimeout = null, 
														CommandType? commandType = null) where T : class {
			    return DatabaseExtension.Impl.SelectAllAsync<T>(Raw, where_clause, param, transaction ?? TxHandle, commandTimeout, commandType);
			}
			public Task<T> SelectAsync<T>(string where_clause, 
											object param = null, 
											IDbTransaction transaction = null, 
											int? commandTimeout = null, 
											CommandType? commandType = null) where T : class {
			    return DatabaseExtension.Impl.SelectAsync<T>(Raw, where_clause, param, transaction ?? TxHandle, commandTimeout, commandType);
			}
			public Task<int> InsertAsync<T>(T entityToInsert, 
												IDbTransaction transaction = null,
            									int? commandTimeout = null, 
            									ISqlAdapter sqlAdapter = null) where T : class {
				return Raw.InsertAsync<T>(entityToInsert, transaction ?? TxHandle, commandTimeout, sqlAdapter);
			}
			public Task<bool> UpdateAsync<T>(T entityToUpdate, 
												IDbTransaction transaction = null, 
												int? commandTimeout = null) where T : class {
				return Raw.UpdateAsync<T>(entityToUpdate, transaction ?? TxHandle, commandTimeout);				
			}
			public Task<bool> DeleteAsync<T>(T entityToDelete, 
												IDbTransaction transaction = null, 
												int? commandTimeout = null) where T : class {
				return Raw.DeleteAsync<T>(entityToDelete, transaction ?? TxHandle, commandTimeout);		
			}
		}
	}
	namespace DatabaseExtension {
		public static class Impl {
			public static async Task<Mtk.Core.AcceptResult> Txn<ERR>(this IDbConnection cnn, System.Func<Database.Conn, Task<Mtk.Core.AcceptResult>> hd) 
				where ERR : Core.IError, new() {
				var c = new Database.Conn(cnn);
				try {
				    var r = await hd(c);
					if (r.Error == null) {
						c.Commit();
					} else {
						c.Rollback();
					}
					return r;
				} catch (System.Exception e) {
					c.Rollback();
					var err = new ERR();
					err.Set(e);
					return new Mtk.Core.AcceptResult{ Cid = 0, Error = err };
				}
			}
			public static async Task<Mtk.Core.HandleResult> Txn<ERR>(this IDbConnection cnn, System.Func<Database.Conn, Task<Mtk.Core.HandleResult>> hd) 
				where ERR : Core.IError, new() {
				var c = new Database.Conn(cnn);
				try {
				    var r = await hd(c);
					if (r.Error == null) {
						c.Commit();
					} else {
						c.Rollback();
					}
					return r;
				} catch (System.Exception e) {
					c.Rollback();
					var err = new ERR();
					err.Set(e);
					return new Mtk.Core.HandleResult{ Error = err };
				}
			}
	        private static readonly ConcurrentDictionary<System.RuntimeTypeHandle, string> TypeTableNameMap = new ConcurrentDictionary<System.RuntimeTypeHandle, string>();
	    	//because it seems that we cannot access private method in other partial implementation...
	        private static string GetOrCacheTableName(System.Type type)
	        {
	        	string name;
	            if (TypeTableNameMap.TryGetValue(type.TypeHandle, out name)) return name;

	            //NOTE: This as dynamic trick should be able to handle both our own Table-attribute as well as the one in EntityFramework 
	            var tableAttr = type
	#if COREFX
	                .GetTypeInfo()
	#endif
	                .GetCustomAttributes(false).SingleOrDefault(attr => attr.GetType().Name == "TableAttribute") as dynamic;
	            if (tableAttr != null)
	            {
	                name = tableAttr.Name;
	            }
	            else
	            {
	                name = type.Name + "s";
	                if (type.IsInterface && name.StartsWith("I"))
	                    name = name.Substring(1);
	            }

	            TypeTableNameMap[type.TypeHandle] = name;
	            return name;
	        }
			public static Task<T> SelectAsync<T>(IDbConnection connection, string where_clause, 
													object param = null, 
													IDbTransaction transaction = null, 
													int? commandTimeout = null,
													CommandType? commandType = null) where T : class {
	            var type = typeof(T);
	            var name = GetOrCacheTableName(type);
	            var sql = $"SELECT * FROM {name} WHERE {where_clause}";
	            return connection.QuerySingleOrDefaultAsync<T>(sql, param, transaction, commandTimeout, commandType);
	        }
			public static Task<IEnumerable<T>> SelectAllAsync<T>(IDbConnection connection, string where_clause, 
																	object param = null, 
																	IDbTransaction transaction = null, 
																	int? commandTimeout = null,
																	CommandType? commandType = null) where T : class {
	            var type = typeof(T);
	            var name = GetOrCacheTableName(type);
	            var sql = $"SELECT * FROM {name} WHERE {where_clause}";
	            return connection.QueryAsync<T>(sql, param, transaction, commandTimeout, commandType);
	        }
	    }
	}
}
