#include "ping.h"
#include <mtk/codec.h>
#include "../proto/test.pb.h"

bool test_ping(mtk_conn_t c) {
	mtk_conn_send(c, test::MessageType::Ping, const char *data, size_t datalen, mtk_callback_t cb)
}

