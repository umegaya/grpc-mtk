
.PHONY: client
client:
	-mkdir -p ./client/build
	cd client/build && (rm client || true) && cmake .. && make

.PHONY: server
server:
	-mkdir -p ./server/build
	cd server/build && (rm server || true) && cmake .. && make

svtest: server
	cd ./server/build && ./server &
	test `curl -d "fuga" http://localhost:8008/hoge` == 'fugahoge'

cltest: client
	cd ./client/build && ./client

build: client server

test: svtest cltest

