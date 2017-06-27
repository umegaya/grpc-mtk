using System.Data;
#if MTKSV
using MySql.Data;
#else
using Mono.Data.Sqlite;
#endif
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Dapper;
using Dapper.Contrib.Extensions;
using SimpleMigrations;

namespace Mtk {
	public partial class Database {
		static public IDbConnection Open(string url) {
			Mtk.Log.Info("Open1");
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
			Mtk.Log.Info("run Migrator");
			Migration.BackendConnection = c;
			var mig = new SimpleMigrator(migrationsAssembly, prov);
			Mtk.Log.Info("create Migrator");
			mig.Load();
			Mtk.Log.Info("load Migrations:" + mig.Migrations.Count);
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
			public async Task<int> ExecuteAsync(string sql, 
											object param = null, 
											IDbTransaction transaction = null, 
											int? commandTimeout = null, 
											CommandType? commandType = null) {
				return await Raw.ExecuteAsync(sql, param, transaction ?? TxHandle, commandTimeout, commandType);
			}
			public async Task<IEnumerable<T>> SelectAllAsync<T>(string sql, 
														object param = null, 
														IDbTransaction transaction = null, 
														int? commandTimeout = null, 
														CommandType? commandType = null)  {
			    return await Raw.QueryAsync<T>(sql, param, transaction ?? TxHandle, commandTimeout, commandType);
			}
			public async Task<T> SelectAsync<T>(string sql, 
											object param = null, 
											IDbTransaction transaction = null, 
											int? commandTimeout = null, 
											CommandType? commandType = null) {
			    return await Raw.QuerySingleOrDefaultAsync<T>(sql, param, transaction ?? TxHandle, commandTimeout, commandType);
			}
			public async Task<int> InsertAsync<T>(T entityToInsert, 
												IDbTransaction transaction = null,
            									int? commandTimeout = null, 
            									ISqlAdapter sqlAdapter = null) where T : class {
				return await Raw.InsertAsync<T>(entityToInsert, transaction ?? TxHandle, commandTimeout, sqlAdapter);
			}
			public async Task<bool> UpdateAsync<T>(T entityToUpdate, 
												IDbTransaction transaction = null, 
												int? commandTimeout = null) where T : class {
				return await Raw.UpdateAsync<T>(entityToUpdate, transaction ?? TxHandle, commandTimeout);				
			}
			public async Task<bool> DeleteAsync<T>(T entityToDelete, 
												IDbTransaction transaction = null, 
												int? commandTimeout = null) where T : class {
				return await Raw.DeleteAsync<T>(entityToDelete, transaction ?? TxHandle, commandTimeout);		
			}
		}
	}
	namespace DatabaseExtension {
		public static class ExtensionImpl {
			public static async Task<Mtk.Core.AcceptResult> Txn<ERR>(this IDbConnection cnn, System.Func<Database.Conn, Task<Mtk.Core.AcceptResult>> hd) 
				where ERR : Core.IError, new() {
				var c = new Database.Conn(cnn);
				try {
				    var r = await hd(c);
					if (r.Error != null) {
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
					if (r.Error != null) {
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
		}
	}
}
