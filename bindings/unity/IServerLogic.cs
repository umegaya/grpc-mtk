
namespace Mtk {
	public interface IServerLogic {
		public Server.AcceptReply OnAccept(SVConn c, ulong cid, byte[] cred);
		public int OnRecv(SVConn c, uint type, byte[] data);
	}
}
