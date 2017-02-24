
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
	echo "TODO: run http client by curl"

cltest: client
	echo "TODO: run http server with nc"
	cd ./client/build && ./client

build: client server