
.PHONY: client
client: testlib
	-mkdir -p ./client/build
	cd client/build && (rm client || true) && cmake .. && make

.PHONY: server
server: testlib
	-mkdir -p ./server/build
	cd server/build && (rm server || true) && cmake .. && make

svtest: server
	cd ./server/build && ./server 4000 &
	sleep 1
	test `curl -d "fuga" http://localhost:8008/hoge` == 'fugahoge'

cltest: client
	cd ./client/build && ./client

testlib:
	[ ! -z "${SKIPLIB}" ] || make -C $(CURDIR)/../.. testlib

build: client server

test: svtest cltest

