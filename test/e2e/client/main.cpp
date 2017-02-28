#include "common.h"
#include "tests/rpc.h"
#include "tests/bench.h"
#include "tests/reconnect.h"

using namespace mtktest;

int main(int argc, char *argv[]) {
	mtk_log_init();
	{
		test t("localhost:50051", test_rpc);
		if (!t.run()) { ALERT_AND_EXIT("test_rpc fails"); }
	}//*/
	{
		test t("localhost:50051", test_reconnect, 32);
		if (!t.run()) { ALERT_AND_EXIT("test_reconnect fails"); }
	}//*/
	{
		test t("localhost:50051", test_reconnect, 4);
		if (!t.run(ConnectPayload::Pending)) { ALERT_AND_EXIT("test_reconnect pending fails"); }
	}//*/
	{
		test t("localhost:50051", test_reconnect, 1);
		if (!t.run(ConnectPayload::Failure, mtk_sec(3))) { ALERT_AND_EXIT("test_reconnect failure fails"); }
	}//*/
	{
		test t("localhost:50051", test_bench, 256);
		if (!t.run()) { ALERT_AND_EXIT("test_bench fails"); }
	}//*/
	return 0;
}

