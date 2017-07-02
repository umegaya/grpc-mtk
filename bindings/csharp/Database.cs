using System.Data;
#if MTKSV
using MySql.Data;
#else
using Mono.Data.Sqlite;
#endif
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Threading;
using System.Linq;
using Dapper;
using Dapper.Contrib.Extensions;
using SimpleMigrations;
using System.Threading.Tasks;
using System.Reflection;

namespace Mtk
{
    public partial class Database
    {
        static public IDbConnection Open(string url)
        {
#if MTKSV
			var c = new MySqlConnection(url);
#else
            var c = new SqliteConnection(url);
#endif
            Migrate(c);
            return c;
        }
        static public void Close(IDbConnection c)
        {
            c.Close();
        }
        static protected void Migrate(System.Data.Common.DbConnection c)
        {
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
        public class Conn
        {
            public IDbConnection Raw;
            public IDbTransaction TxHandle;
            internal Conn(IDbConnection raw)
            {
                Raw = raw;
                TxHandle = raw.BeginTransaction();
            }
            internal void Commit() { TxHandle.Commit(); }
            internal void Rollback() { TxHandle.Rollback(); }
            //caution: you specify correct table name by yourself
            public int Execute(string sql,
                                            object param = null,
                                            IDbTransaction transaction = null,
                                            int? commandTimeout = null,
                                            CommandType? commandType = null)
            {
                return Raw.Execute(sql, param, transaction ?? TxHandle, commandTimeout, commandType);
            }
            public IEnumerable<T> SelectAll<T>(string where_clause,
                                                        object param = null,
                                                        IDbTransaction transaction = null,
                                                        int? commandTimeout = null,
                                                        CommandType? commandType = null) where T : class
            {
                return DatabaseExtension.Impl.SelectAll<T>(Raw, where_clause, param, transaction ?? TxHandle, commandTimeout, commandType);
            }
            public T Select<T>(string where_clause,
                                            object param = null,
                                            IDbTransaction transaction = null,
                                            int? commandTimeout = null,
                                            CommandType? commandType = null) where T : class
            {
                return DatabaseExtension.Impl.Select<T>(Raw, where_clause, param, transaction ?? TxHandle, commandTimeout, commandType);
            }
            public long Insert<T>(T entityToInsert,
                                                IDbTransaction transaction = null,
                                                int? commandTimeout = null) where T : class
            {
#if MTK_DISABLE_ASYNC
				return Raw.Insert<T>(entityToInsert, transaction ?? TxHandle, commandTimeout, DatabaseExtension.Impl.InsertAdaptor);
#else
                return Raw.Insert<T>(entityToInsert, transaction ?? TxHandle, commandTimeout);
#endif
            }
            public bool Update<T>(T entityToUpdate,
                                                IDbTransaction transaction = null,
                                                int? commandTimeout = null) where T : class
            {
#if MTK_DISABLE_ASYNC
				return Raw.Update<T>(entityToUpdate, transaction ?? TxHandle, commandTimeout);				
#else
                return Raw.Update<T>(entityToUpdate, transaction ?? TxHandle, commandTimeout);
#endif
            }
            public bool Delete<T>(T entityToDelete,
                                                IDbTransaction transaction = null,
                                                int? commandTimeout = null) where T : class
            {
                return Raw.Delete<T>(entityToDelete, transaction ?? TxHandle, commandTimeout);
            }
#if !MTK_DISABLE_ASYNC
            //caution: you specify correct table name by yourself
            public Task<int> ExecuteAsync(string sql,
                                            object param = null,
                                            IDbTransaction transaction = null,
                                            int? commandTimeout = null,
                                            CommandType? commandType = null)
            {
                return Raw.ExecuteAsync(sql, param, transaction ?? TxHandle, commandTimeout, commandType);
            }
            public Task<IEnumerable<T>> SelectAllAsync<T>(string where_clause,
                                                        object param = null,
                                                        IDbTransaction transaction = null,
                                                        int? commandTimeout = null,
                                                        CommandType? commandType = null) where T : class
            {
                return DatabaseExtension.Impl.SelectAllAsync<T>(Raw, where_clause, param, transaction ?? TxHandle, commandTimeout, commandType);
            }
            public Task<T> SelectAsync<T>(string where_clause,
                                            object param = null,
                                            IDbTransaction transaction = null,
                                            int? commandTimeout = null,
                                            CommandType? commandType = null) where T : class
            {
                return DatabaseExtension.Impl.SelectAsync<T>(Raw, where_clause, param, transaction ?? TxHandle, commandTimeout, commandType);
            }
            public Task<int> InsertAsync<T>(T entityToInsert,
                                                IDbTransaction transaction = null,
                                                int? commandTimeout = null,
                                                ISqlAdapter sqlAdapter = null) where T : class
            {
                return Raw.InsertAsync<T>(entityToInsert, transaction ?? TxHandle, commandTimeout, sqlAdapter);
            }
            public Task<bool> UpdateAsync<T>(T entityToUpdate,
                                                IDbTransaction transaction = null,
                                                int? commandTimeout = null) where T : class
            {
                return Raw.UpdateAsync<T>(entityToUpdate, transaction ?? TxHandle, commandTimeout);
            }
            public Task<bool> DeleteAsync<T>(T entityToDelete,
                                                IDbTransaction transaction = null,
                                                int? commandTimeout = null) where T : class
            {
                return Raw.DeleteAsync<T>(entityToDelete, transaction ?? TxHandle, commandTimeout);
            }
#endif
        }
    }
    namespace DatabaseExtension
    {
#if MTK_DISABLE_ASYNC
		public partial class SQLiteAdapter : ISqlAdapter {
			public long Insert(IDbConnection connection, IDbTransaction transaction, int? commandTimeout, string tableName, 
								string columnList, string parameterList, IEnumerable<PropertyInfo> keyProperties, object entityToInsert) {
				var cmd = string.Format("insert into {0} ({1}) values ({2})", tableName, columnList, parameterList);

				connection.Execute(cmd, entityToInsert, transaction, commandTimeout, null);

				var r = connection.Query("select last_insert_rowid() id", null, transaction, true, commandTimeout, null);
				var id = (long)r.First()["id"];
				var propertyInfos = keyProperties as PropertyInfo[] ?? keyProperties.ToArray();
				if (propertyInfos.Any()) {
					if (propertyInfos.First().PropertyType == typeof(ulong)) {
					    propertyInfos.First().SetValue(entityToInsert, (ulong)id, null);
					} else {
					    propertyInfos.First().SetValue(entityToInsert, id, null);						
					}
				}
				return id;
			}
		}
#endif
        public static class Impl
        {
            public static ISqlAdapter InsertAdaptor { get; set; }
            private static readonly ConcurrentDictionary<System.RuntimeTypeHandle, string> TypeTableNameMap = new ConcurrentDictionary<System.RuntimeTypeHandle, string>();

            //because it seems that we cannot access private method in other partial implementation...
            private static string GetOrCacheTableName(System.Type type)
            {
                string name;
                if (TypeTableNameMap.TryGetValue(type.TypeHandle, out name)) return name;

                //NOTE: This as dynamic trick should be able to handle both our own Table-attribute as well as the one in EntityFramework 
                var tableAttr = type
                    .GetCustomAttributes(false).SingleOrDefault(attr => attr.GetType().Name == "TableAttribute");
                if (tableAttr != null)
                {
                    name = (tableAttr as TableAttribute).Name;
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
#if MTK_DISABLE_ASYNC
	        public static void AllowUInt64PrimeryKeyForNet35() { InsertAdaptor = new SQLiteAdapter(); }
#endif
            public static Mtk.Core.AcceptResult Txn<ERR>(this IDbConnection cnn, System.Func<Database.Conn, Mtk.Core.AcceptResult> hd) 
				where ERR : Core.IError, new() {
				var c = new Database.Conn(cnn);
				try {
				    var r = hd(c);
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
			public static Mtk.Core.HandleResult Txn<ERR>(this IDbConnection cnn, System.Func<Database.Conn, Mtk.Core.HandleResult> hd) 
				where ERR : Core.IError, new() {
				var c = new Database.Conn(cnn);
				try {
				    var r = hd(c);
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
			public static T Select<T>(IDbConnection connection, string where_clause, 
													object param = null, 
													IDbTransaction transaction = null, 
													int? commandTimeout = null,
													CommandType? commandType = null) where T : class {
	            var type = typeof(T);
	            var name = GetOrCacheTableName(type);
	            var sql = "SELECT * FROM " + name + " WHERE " + where_clause;
#if MTK_DISABLE_ASYNC
	            var r = connection.Query<T>(sql, param, transaction, true, commandTimeout, commandType);
	            return r.FirstOrDefault();
#else
	            return connection.QuerySingleOrDefault<T>(sql, param, transaction, commandTimeout, commandType);
#endif
	        }
			public static IEnumerable<T> SelectAll<T>(IDbConnection connection, string where_clause, 
																	object param = null, 
																	IDbTransaction transaction = null, 
																	int? commandTimeout = null,
																	CommandType? commandType = null) where T : class {
	            var type = typeof(T);
	            var name = GetOrCacheTableName(type);
	            var sql = "SELECT * FROM " + name + " WHERE " + where_clause;
                return connection.Query<T>(sql, param, transaction, true, commandTimeout, commandType);
	        }
#if !MTK_DISABLE_ASYNC
			public static async Task<Mtk.Core.AcceptResult> TxnAsync<ERR>(this IDbConnection cnn, System.Func<Database.Conn, Task<Mtk.Core.AcceptResult>> hd) 
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
			public static async Task<Mtk.Core.HandleResult> TxnAsync<ERR>(this IDbConnection cnn, System.Func<Database.Conn, Task<Mtk.Core.HandleResult>> hd) 
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
#endif
	    }
	}
}
