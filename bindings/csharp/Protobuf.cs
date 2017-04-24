using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Google.Protobuf;

//protobuf extension of Mtk
namespace Mtk {
	public class Codec {
    	static public int Pack(IMessage m, out byte[] bytes) {
            var len = m.CalculateSize();
            bytes = new byte[len];
    		var ous = new CodedOutputStream(bytes);
    		try {
	    		m.WriteTo(ous);
    		} catch (InvalidProtocolBufferException e) {
                Mtk.Log.Error("ev:packerr,msg:" + e.Message);
				return -1;    			
    		}
    		return (int)ous.Position;
    	}
    	static public int Unpack<T>(byte[] bytes, ref T m) where T : IMessage {
    		var ins = new CodedInputStream(bytes, 0, bytes.Length);
    		try {
	    		m.MergeFrom(ins);
    		} catch (InvalidProtocolBufferException e) {
                Mtk.Log.Error("ev:unpackerr,msg:" + e.Message);
				return -1;    			
    		}
    		return (int)ins.Position;
    	}
    	static unsafe public int Unpack<T>(byte* bytes, uint len, ref T m) where T : IMessage {
    		byte[] arr = new byte[len];
			Marshal.Copy((System.IntPtr)bytes, arr, 0, (int)len);
			return Unpack(arr, ref m);
    	}
	}
    public partial class Core {
        public partial interface ISVConn {
            bool Reply(uint msgid, IMessage m);
            bool Task(uint type, IMessage m);
            bool Throw(uint msgid, IMessage m);
            bool Notify(uint type, IMessage m);            
        }
        public partial class SVConn {
            public bool Reply(uint msgid, IMessage m) {
                byte[] data;
                if (Codec.Pack(m, out data) < 0) { Core.Assert(false); return false; }
                Reply(msgid, data);
                return true;
            }
            public bool Task(uint type, IMessage m) {
                byte[] data;
                if (Codec.Pack(m, out data) < 0) { Core.Assert(false); return false; }
                Task(type, data);
                return true;
            }
            public bool Throw(uint msgid, IMessage m) {
                byte[] data;
                if (Codec.Pack(m, out data) < 0) { Core.Assert(false); return false; }
                Throw(msgid, data);
                return true;
            }
            public bool Notify(uint type, IMessage m) {
                byte[] data;
                if (Codec.Pack(m, out data) < 0) { Core.Assert(false); return false; }
                Notify(type, data);
                return true;
            }
            static public bool Reply<T>(ulong cid, uint msgid, T data) where T : IMessage {
                byte[] payload;
                if (Codec.Pack(data, out payload) < 0) { Core.Assert(false); return false; }
                Reply(cid, msgid, payload);
                return true;
            }
            static public bool Throw<T>(ulong cid, uint msgid, T data) where T : IMessage {
                byte[] payload;
                if (Codec.Pack(data, out payload) < 0) { Core.Assert(false); return false; }
                Throw(cid, msgid, payload);
                return true;
            }
            static public bool Notify<T>(ulong cid, uint type, T data) where T : IMessage {
                byte[] payload;
                if (Codec.Pack(data, out payload) < 0) { Core.Assert(false); return false; }
                Notify(cid, type, payload);
                return true;
            }
            static public void Reply(ulong cid, uint msgid, ByteString data) {
                unsafe { fixed (byte* d = data.UnsafeBuffer) { mtk_cid_send(cid, msgid, d, (uint)data.Length); } }
            }
            static public void Throw(ulong cid, uint msgid, ByteString data) {
                unsafe { fixed (byte* d = data.UnsafeBuffer) { mtk_cid_error(cid, msgid, d, (uint)data.Length); } }
            }
            static public void Notify(ulong cid, uint type, ByteString data) {
                unsafe { fixed (byte* d = data.UnsafeBuffer) { mtk_cid_notify(cid, type, d, (uint)data.Length); } }
            }
        }
        public partial class CidConn {
            public bool Reply(uint msgid, IMessage m) {
                byte[] data;
                if (Codec.Pack(m, out data) < 0) { Core.Assert(false); return false; }
                Reply(msgid, data);
                return true;
            }
            public bool Task(uint type, IMessage m) {
                byte[] data;
                if (Codec.Pack(m, out data) < 0) { Core.Assert(false); return false; }
                Task(type, data);
                return true;
            }
            public bool Throw(uint msgid, IMessage m) {
                byte[] data;
                if (Codec.Pack(m, out data) < 0) { Core.Assert(false); return false; }
                Throw(msgid, data);
                return true;
            }
            public bool Notify(uint type, IMessage m) {
                byte[] data;
                if (Codec.Pack(m, out data) < 0) { Core.Assert(false); return false; }
                Notify(type, data);
                return true;
            }
        }
    }
}
