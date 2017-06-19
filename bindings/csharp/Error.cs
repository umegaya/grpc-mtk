namespace Mtk {
    public partial class Core {
    	public enum SystemErrorCode {
    		Success = 0,
    		PayloadPackFail = -1,
    		PayloadUnpackFail = -2,
    	};
    	public interface IError {
    		//indicate handler should not reply immediately
    		bool Pending { get; }
    		string Message { get; }
			void Set(System.Exception e);
			void Set(Mtk.Core.SystemErrorCode ec);
    	};
    }
}
