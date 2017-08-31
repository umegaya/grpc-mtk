using System.Data;
#if MTKSV
using MySql.Data;
using MySql.Data.MySqlClient;
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
        string ConnectUrl;
#if !MTKSV
        System.Data.Common.DbConnection Connection = null;
#endif
        public Database(string url, Assembly mig_assembly = null)
        {
            ConnectUrl = url;
            using (var c = Create(url)) {
                Migrate(c, mig_assembly);
            }
#if MTKSV
            Conn.QueryTrait = new DatabaseExtension.Impl.MySqlQueryTrait();
#else
            Conn.QueryTrait = new DatabaseExtension.Impl.SqliteQueryTrait();
#endif
        }
        internal IDbConnection NewConn() {
#if MTKSV
            var c = Create(ConnectUrl);
            c.Open();
            return c;
#else
            if (Connection == null) {
                Connection = Create(ConnectUrl);
                Connection.Open();
            }
            return Connection;
#endif
        }
        static internal System.Data.Common.DbConnection Create(string url) {
#if MTKSV
            return new MySqlConnection(url);
#else
            return new SqliteConnection(url);
#endif
        }
        static internal void Migrate(System.Data.Common.DbConnection c, Assembly mig_assembly)
        {
            c.Open();
            IDatabaseProvider<System.Data.Common.DbConnection> prov;
            if (mig_assembly == null) {
                mig_assembly = typeof(Database).Assembly;
            }
#if MTKSV
			prov = new SimpleMigrations.DatabaseProvider.MysqlDatabaseProvider(c);
#else
            prov = new SimpleMigrations.DatabaseProvider.SqliteDatabaseProvider(c);
#endif
            Migration.BackendConnection = c;
            var mig = new SimpleMigrator(mig_assembly, prov);
            mig.Load();
            mig.MigrateToLatest();
        }
#if !MTK_DISABLE_ASYNC
		public async Task<Mtk.Core.AcceptResult> TxnAsync<ERR>(System.Func<Database.Conn, Task<Mtk.Core.AcceptResult>> hd)
			where ERR : Core.IError, new()
		{
			var c = new Conn(this);
			try
			{
				var r = await hd(c);
				if (r.Error == null)
				{
					c.Commit();
				}
				else
				{
					c.Rollback();
				}
				return r;
			}
			catch (System.Exception e)
			{
				c.Rollback();
				var err = new ERR();
				err.Set(e);
				return new Mtk.Core.AcceptResult { Cid = 0, Error = err };
			}
		}
		public async Task<Mtk.Core.HandleResult> TxnAsync<ERR>(System.Func<Database.Conn, Task<Mtk.Core.HandleResult>> hd)
			where ERR : Core.IError, new()
		{
			var c = new Conn(this);
			try
			{
				var r = await hd(c);
				if (r.Error == null)
				{
					c.Commit();
				}
				else
				{
					c.Rollback();
				}
				return r;
			}
			catch (System.Exception e)
			{
				c.Rollback();
				var err = new ERR();
				err.Set(e);
				return new Mtk.Core.HandleResult { Error = err };
			}
		}
#endif
        public ERR Txn<ERR>(System.Action<Database.Conn> hd)
            where ERR : Core.IError, new()
        {
            var c = new Conn(this);
            try
            {
                hd(c);
                c.Commit();
                return default(ERR);
            }
            catch (System.Exception e)
            {
                c.Rollback();
                var err = new ERR();
                err.Set(e);
                return err;
            }
        }
        public Mtk.Core.AcceptResult Txn<ERR>(System.Func<Database.Conn, Mtk.Core.AcceptResult> hd)
			where ERR : Core.IError, new()
		{
			var c = new Conn(this);
			try
			{
				var r = hd(c);
				if (r.Error == null)
				{
					c.Commit();
				}
				else
				{
					c.Rollback();
				}
				return r;
			}
			catch (System.Exception e)
			{
				c.Rollback();
				var err = new ERR();
				err.Set(e);
				return new Mtk.Core.AcceptResult { Cid = 0, Error = err };
			}
		}
		public Mtk.Core.HandleResult Txn<ERR>(System.Func<Database.Conn, Mtk.Core.HandleResult> hd)
			where ERR : Core.IError, new()
		{
			var c = new Conn(this);
			try
			{
				var r = hd(c);
				if (r.Error == null)
				{
					c.Commit();
				}
				else
				{
					c.Rollback();
				}
				return r;
			}
			catch (System.Exception e)
			{
				c.Rollback();
				var err = new ERR();
				err.Set(e);
				return new Mtk.Core.HandleResult { Error = err };
			}
		}
        public class Conn
        {
            public IDbConnection Raw;
            public IDbTransaction TxHandle;
            public static DatabaseExtension.Impl.IQueryTrait QueryTrait;
            internal Conn(Database db)
            {
                Raw = db.NewConn();
                TxHandle = Raw.BeginTransaction();
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
            public bool Insert<T>(T entityToInsert,
                                                IDbTransaction transaction = null,
                                                int? commandTimeout = null) where T : class
            {
                return DatabaseExtension.Impl.Insert<T,ulong>(Raw, entityToInsert, QueryTrait, transaction ?? TxHandle, commandTimeout);
            }
            public int Update<T>(T entityToUpdate,
                                                string where_clause = null,
                                                IDbTransaction transaction = null,
                                                int? commandTimeout = null) where T : class
            {
				return DatabaseExtension.Impl.Update<T>(Raw, entityToUpdate, where_clause, transaction ?? TxHandle, commandTimeout);				
            }
            /*public bool Delete<T>(T entityToDelete,
                                                IDbTransaction transaction = null,
                                                int? commandTimeout = null) where T : class
            {
                return Raw.Delete<T>(entityToDelete, transaction ?? TxHandle, commandTimeout);
            }*/
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
            public Task<bool> InsertAsync<T>(T entityToInsert,
                                                IDbTransaction transaction = null,
                                                int? commandTimeout = null) where T : class
            {
                return DatabaseExtension.Impl.InsertAsync<T,ulong>(Raw, entityToInsert, QueryTrait, transaction ?? TxHandle, commandTimeout);
            }
            public Task<int> UpdateAsync<T>(T entityToUpdate,
                                                string where_clause = null,
                                                IDbTransaction transaction = null,
                                                int? commandTimeout = null) where T : class
            {
                return DatabaseExtension.Impl.UpdateAsync<T>(Raw, entityToUpdate, where_clause, transaction ?? TxHandle, commandTimeout);
            }
#endif
        }
    }
    namespace DatabaseExtension
    {
        public static class Impl
        {
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

            //database dependent primary key querier
            public interface IQueryTrait {
                string LastInsertId { get; }
            }
            public class SqliteQueryTrait : IQueryTrait {
                public string LastInsertId {
                    get { return "select last_insert_rowid() id"; }
                }
            }
            public class MySqlQueryTrait : IQueryTrait {
                public string LastInsertId {
                    get { return "select last_insert_id() as id"; }
                }
            }

            //select
            private static string SelectStmt<T>(string where_clause) {
                var type = typeof(T);
                var name = GetOrCacheTableName(type);
                var sql = "SELECT * FROM " + name + " WHERE " + where_clause;
                return sql;
            }
			public static T Select<T>(IDbConnection connection, string where_clause, 
													object param = null, 
													IDbTransaction transaction = null, 
													int? commandTimeout = null,
													CommandType? commandType = null) where T : class {
	            var sql = SelectStmt<T>(where_clause);
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
	            var sql = SelectStmt<T>(where_clause);
                return connection.Query<T>(sql, param, transaction, true, commandTimeout, commandType);
	        }

            //insert
            private static string InsertStmt<T>(out Database.ColumnAttribute attr) {
                var type = typeof(T);
                var name = GetOrCacheTableName(type);
                var columns = Database.SelectColumns(type);
                attr = null;
                if (columns.Length <= 0) {
                    return "";
                }
                var columnlist = "";
                var paramlist = "";
                var prefix = "";
                var first = true;
                for (var i = 0; i < columns.Length; i++) { //TODO: somehow skip unchanged columnes
                    if (columns[i].Name != "Id") {
                        columnlist += (prefix + columns[i].Name);
                        paramlist += (prefix + "@" + columns[i].PropName);
                        if (first) {
                            prefix = ",";
                            first = false;
                        }
                    } else {
                        attr = columns[i];
                    }
                }
                var sql = "INSERT INTO " + name + "(" + columnlist + ") VALUES (" + paramlist + ")"; 
                return sql;
            }
            public static bool Insert<T,PKEY>(IDbConnection connection, T entityToInsert,
                                                IQueryTrait queryTrait, 
                                                IDbTransaction transaction = null,
                                                int? commandTimeout = null, 
                                                CommandType? commandType = null) {
                Database.ColumnAttribute attr;
                var sql = InsertStmt<T>(out attr);
                var cnt = connection.Execute(sql, entityToInsert, transaction, commandTimeout, commandType);
                if (cnt > 0 && attr != null) {
                    var r = connection.Query<PKEY>(queryTrait.LastInsertId, null, transaction, true, commandTimeout, null);
                    var p = Database.SelectProperty(typeof(T), attr.PropName);
                    if (p != null) {
                        p.SetValue(entityToInsert, r.First(), null);
                    }
                }
                return cnt > 0;
            }

            //update
            private static string UpdateStmt<T>(string where_clause = null) {
                var type = typeof(T);
                var name = GetOrCacheTableName(type);
                var columns = Database.SelectColumns(type);
                if (columns.Length <= 0) {
                    return "";
                }
                var first = true;
                var idcolumn = "Id";
                var updatelist = "";
                var prefix = "";
                for (var i = 0; i < columns.Length; i++) { //TODO: somehow skip unchanged columnes
                    if (columns[i].Name == "Id") {
                        if (where_clause == null) {
                             idcolumn = columns[i].PropName;
                        }
                    } else {
                        updatelist += (prefix + columns[i].Name + "=@" + columns[i].PropName);
                        if (first) {
                            prefix = ",";
                            first = false;
                        }
                    }
                }
                var sql = "UPDATE " + name + " SET " + updatelist + " WHERE " + (where_clause ?? (" Id = @" + idcolumn)); 
                return sql;
            }            
            public static int Update<T>(IDbConnection connection, T entityToUpdate,
                                                string where_clause = null,
                                                IDbTransaction transaction = null,
                                                int? commandTimeout = null, 
                                                CommandType? commandType = null) {
                var sql = UpdateStmt<T>(where_clause);
                return connection.Execute(sql, entityToUpdate, transaction, commandTimeout, commandType);
            }
#if !MTK_DISABLE_ASYNC
			public static Task<T> SelectAsync<T>(IDbConnection connection, string where_clause, 
													object param = null, 
													IDbTransaction transaction = null, 
													int? commandTimeout = null,
													CommandType? commandType = null) where T : class {
                var sql = SelectStmt<T>(where_clause);
	            return connection.QuerySingleOrDefaultAsync<T>(sql, param, transaction, commandTimeout, commandType);
	        }
			public static Task<IEnumerable<T>> SelectAllAsync<T>(IDbConnection connection, string where_clause, 
																	object param = null, 
																	IDbTransaction transaction = null, 
																	int? commandTimeout = null,
																	CommandType? commandType = null) where T : class {
                var sql = SelectStmt<T>(where_clause);
	            return connection.QueryAsync<T>(sql, param, transaction, commandTimeout, commandType);
	        }
            public static async Task<bool> InsertAsync<T,PKEY>(IDbConnection connection, T entityToInsert,
                                                IQueryTrait queryTrait, 
                                                IDbTransaction transaction = null,
                                                int? commandTimeout = null, 
                                                CommandType? commandType = null) {
                Database.ColumnAttribute attr;
                var sql = InsertStmt<T>(out attr);
                var cnt = await connection.ExecuteAsync(sql, entityToInsert, transaction, commandTimeout, commandType);
                    Mtk.Log.Info("result:" + cnt + "|" + (attr != null));
                if (cnt > 0 && attr != null) {
                    var r = await connection.QueryAsync<PKEY>(queryTrait.LastInsertId, null, transaction, commandTimeout, null);
                    Mtk.Log.Info("rfirst:" + r.First());
                    var p = Database.SelectProperty(typeof(T), attr.Name);
                    if (p != null) {
                        p.SetValue(entityToInsert, r.First(), null);
                    } else {
                        Mtk.Log.Info("cannot find prop:" + attr.Name);
                    }
                }
                return cnt > 0;
            }
            public static Task<int> UpdateAsync<T>(IDbConnection connection, T entityToUpdate,
                                                string where_clause = null,
                                                IDbTransaction transaction = null,
                                                int? commandTimeout = null, 
                                                CommandType? commandType = null) {
                var sql = UpdateStmt<T>(where_clause);
                return connection.ExecuteAsync(sql, entityToUpdate, transaction, commandTimeout, commandType);
            }
#endif
	    }
	}
}
