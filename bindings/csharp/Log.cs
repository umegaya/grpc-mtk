namespace Mtk {
    public partial class Core {
    	public enum LogLevel {
            Trace,
            Debug,
            Info,
            Warn,
            Error,
            Fatal,            
            Report,
        };
        public static void InitLogger(string name, LogWriteCB writer) {
            mtk_log_config(name, writer);
        }
        public static void Log(LogLevel lv, string str) {
#if UNITY_EDITOR
            switch (lv) {
            case Core.LogLevel.Trace:
            case Core.LogLevel.Debug:
                UnityEngine.Debug.Log(str);
                break;
            case Core.LogLevel.Info:
            case Core.LogLevel.Report:
                UnityEngine.Debug.Log(str + ",lv_:" + lv);
                break;
            case Core.LogLevel.Warn:
                UnityEngine.Debug.LogWarning(str + ",lv_:" + lv);
                break;
            case Core.LogLevel.Error:
            case Core.LogLevel.Fatal:
                UnityEngine.Debug.LogError(str + ",lv_:" + lv);
                break;
            }
#else
            mtk_log((int)lv, str);
#endif
        }
        public static void LogFlush() {
            mtk_log_flush();
        }
        public static void Assert(bool expr) {
            if (!expr) {
                var st = new  System.Diagnostics.StackTrace(1, true);
                Log(LogLevel.Fatal, "assertion fails at " + st.ToString());
                //TODO: real abortion
            }
        }
	}
    public class Log {
        public static void Write(Core.LogLevel lv, string str) {
            Core.Log(lv, str);
        }
        public static void Info(string str) {
            Core.Log(Core.LogLevel.Info, str);
        }
        public static void Error(string str) {
            Core.Log(Core.LogLevel.Error, str);
        }
        public static void Debug(string str) {
            Core.Log(Core.LogLevel.Debug, str);
        }
        public static void Fatal(string str) {
            Core.Log(Core.LogLevel.Fatal, str);
        }
        public static void Report(string str) {
            Core.Log(Core.LogLevel.Report, str);
        }
        public static void Flush() {
            Core.LogFlush();
        }
    }
}
