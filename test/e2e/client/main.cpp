#include "common.h"
#include "tests/rpc.h"
#include "tests/bench.h"
#include "tests/reconnect.h"

using namespace mtktest;

void testsuites(const char *addr, bool skip = true) {
	{
		test t(addr, test_rpc);
		if (!t.run()) { ALERT_AND_EXIT("test_rpc fails"); }
	}//*/
	{
		test t(addr, test_reconnect, 32);
		if (!t.run()) { ALERT_AND_EXIT("test_reconnect fails"); }
	}//*/
	{
		test t(addr, test_reconnect, 4);
		if (!t.run(ConnectPayload::Pending)) { ALERT_AND_EXIT("test_reconnect pending fails"); }
	}//*/
	{
		test t(addr, test_reconnect, 1);
		if (!t.run(ConnectPayload::Failure, mtk_sec(7))) { ALERT_AND_EXIT("test_reconnect failure fails"); }
	}//*/
	if (skip) {
		test t(addr, test_bench, 256);
		if (!t.run()) { ALERT_AND_EXIT("test_bench fails"); }
	}//*/
}

int main(int argc, char *argv[]) {
	mtk_log_init();
	TRACE("============== test with normal mode ==============");
	testsuites("localhost:50051");
	TRACE("============== test with queue mode ==============");
	testsuites("localhost:50052", false);
	return 0;
}

