#include "common.h"
#include "tests/rpc.h"
#include "tests/bench.h"
#include "tests/reconnect.h"

using namespace mtktest;

int main(int argc, char *argv[]) {
	mtk_log_init();

	test t("localhost:50051", test_rpc);
	if (!t.run()) { ALERT_AND_EXIT("test_rpc fails"); }

	test t3("localhost:50051", test_reconnect, 32);
	if (!t3.run()) { ALERT_AND_EXIT("test_reconnect fails"); }

	test t2("localhost:50051", test_bench, 256);
	if (!t2.run()) { ALERT_AND_EXIT("test_bench fails"); }

	return 0;
}

