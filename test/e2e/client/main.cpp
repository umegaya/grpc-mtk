#include "common.h"
#include "tests/rpc.h"

using namespace mtktest;

void test_rpc(mtk_conn_t c, test &t) {
	test_ping(c, t.done_signal());
}
void test_bench(mtk_conn_t c, test &t) {
}
void test_reconnection(mtk_conn_t c, test &t) {
}
int main(int argc, char *argv[]) {
	mtk_log_init();
	
	test t("localhost:50051", test_rpc);
	t.run();

	//test t2("localhost:50051", test_bench);
	//t2.run();

	//test t3("localhost:50051", test_reconnection);
	//t3.run();
}

