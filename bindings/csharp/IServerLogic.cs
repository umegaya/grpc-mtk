
namespace Mtk {
	public interface IServerLogic {
		public ulong OnAccept(ulong cid, byte[] data, out byte[] rep);
		public int OnRecv(ISVConn c, uint type, byte[] data);
		public void OnClose(ulong cid);
	}
}
