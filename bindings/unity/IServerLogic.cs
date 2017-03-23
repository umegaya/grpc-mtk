
namespace Mtk {
	public interface IServerLogic {
		public Server.AcceptReply OnAccept(ulong cid, byte[] data);
		public int OnRecv(ISVConn c, uint type, byte[] data);
	}
}
