using System.Data;
using SimpleMigrations;
using FluentMigrator.Builders.Alter;
using FluentMigrator.Builders.Create;
using FluentMigrator.Builders.Insert;
using FluentMigrator.Builders.Rename;
using FluentMigrator.Builders.Schema;
using FluentMigrator.Builders.Delete;
using FluentMigrator.Builders.Execute;
using FluentMigrator.Builders.Update;
using FluentMigrator.Infrastructure;
using FluentMigrator;

namespace Mtk {
	public partial class Database {
		public class MigrationOptions : FluentMigrator.IMigrationProcessorOptions{
			public bool PreviewOnly { get; set; }
			public int Timeout { get; set; }
			public string ProviderSwitches { get; set; }
			public MigrationOptions() {
				PreviewOnly = false;
				Timeout = 30;
			}
		}
		public class NullAnnouncer : FluentMigrator.Runner.IAnnouncer {
			public void Heading(string message) { Mtk.Log.Info("ev:migrate Heading,msg:" + message); }
			public void Say(string message) { Mtk.Log.Info("ev:migrate Heading,msg:" + message); }
			public void Emphasize(string message) { Mtk.Log.Info("ev:migrate Heading,msg:" + message); }
			public void Sql(string sql) { Mtk.Log.Info("ev:migrate Sql,msg:" + sql); }
			public void ElapsedTime(System.TimeSpan timeSpan) { Mtk.Log.Info("ev:migrate Takes Time,time:" + timeSpan); }
			public void Error(string message) { Mtk.Log.Error("ev:migrate Error,msg:" + message); }
			public void Error(System.Exception e) { Mtk.Log.Error("ev:migrate Exception,msg:" + e.Message + " at " + e.StackTrace); }
			public void Write(string message, bool escaped) { Mtk.Log.Info("ev:migrate Heading,msg:" + message); }
		}
		public class MySqlProcessor : FluentMigrator.Runner.Processors.MySql.MySqlProcessor {
			Migration migrator_;
	        public MySqlProcessor(Migration m, IMigrationProcessorOptions options)
	            : base(Migration.BackendConnection, new FluentMigrator.Runner.Generators.MySql.MySqlGenerator(), new NullAnnouncer(), options, null) {
	        	migrator_ = m;
	        }
			protected override void Process(string sql) {
				//Mtk.Log.Info("mysql:Process:sql = " + sql);
				migrator_.ExecSql(sql, Options.Timeout);				
			}
		}
		public class SQLiteProcessor : FluentMigrator.Runner.Processors.SQLite.SQLiteProcessor {
			Migration migrator_;
	        public SQLiteProcessor(Migration m, IMigrationProcessorOptions options)
	            : base(Migration.BackendConnection, new FluentMigrator.Runner.Generators.SQLite.SQLiteGenerator(), new NullAnnouncer(), options, null) {
	        	migrator_ = m;
	        }
			protected override void Process(string sql) {
				//Mtk.Log.Info("sqlite:Process:sql = " + sql);
				migrator_.ExecSql(sql, Options.Timeout);
			}
		}
		public abstract class Migration : SimpleMigrations.Migration {
			public abstract void Forward();
			public abstract void Back();

			public Migration() {
				Context = new MigrationContext(null, null, null);
	            Alter = new AlterExpressionRoot(Context); 
				Create = new CreateExpressionRoot(Context);
				Rename = new RenameExpressionRoot(Context);
				Insert = new InsertExpressionRoot(Context);
	        	Schema = new SchemaExpressionRoot(Context); 
                Delete = new DeleteExpressionRoot(Context); 	
                Execute = new ExecuteExpressionRoot(Context);
                Update = new UpdateExpressionRoot(Context); 
#if MTKSV
                Processor = new MySqlProcessor(this, new MigrationOptions());
#else
                Processor = new SQLiteProcessor(this, new MigrationOptions());
#endif
	        }
	        public override void Up() {
	        	Forward();
	        	ApplyChanges();
	        }
	        public override void Down() {
	        	Back();
	        	ApplyChanges();
	        }
	        private void ApplyChanges() {
	        	foreach (var expression in Context.Expressions) {
	                try {
	                	expression.ExecuteWith(Processor);
	                } catch (System.Exception e) {
	                	Mtk.Log.Error("ev:migration error,msg:" + e.Message);
	                }
	            }
	        }
	        private IMigrationContext Context { get; set; }
	        private IMigrationProcessor Processor { get; set; }
	        public IAlterExpressionRoot Alter { get; set; }
	        public ICreateExpressionRoot Create { get; set; }
	        public IRenameExpressionRoot Rename { get; set; }
	        public IInsertExpressionRoot Insert { get; set; }
	        public ISchemaExpressionRoot Schema { get; set; }
			public IDeleteExpressionRoot Delete { get; set; }
        	public new IExecuteExpressionRoot Execute { get; set; }
        	public IUpdateExpressionRoot Update { get; set; }
        	static public IDbConnection BackendConnection { get; set; }

        	public void ExecSql(string sql, int? timeout) {
        		base.Execute(sql, timeout);
        	}
        }
	}
}
