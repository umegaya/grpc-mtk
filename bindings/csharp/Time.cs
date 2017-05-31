namespace Mtk {
    public partial class Core {
        static public ulong Tick { 
            get { return mtk_time(); } 
        }
        static public uint Time {
            get { return (uint)mtk_second(); }
        }
        static public void Sleep(ulong d) {
            mtk_pause(d);
        }
        static public ulong Sec2Tick(uint sec) { return ((ulong)sec) * 1000 * 1000 * 1000; }
        static public ulong FSec2Tick(float sec) { return ((ulong)(sec * 1000f * 1000f)) * 1000; }
        static public ulong MSec2Tick(uint msec) { return ((ulong)msec) * 1000 * 1000; }
        static public ulong USec2Tick(uint usec) { return ((ulong)usec) * 1000; }
        static public ulong NSec2Tick(uint nsec) { return ((ulong)nsec); }
        static public uint Tick2Sec(ulong tick) { return (uint)(tick / (1000 * 1000 * 1000)); }
	}
}