client:
	-mkdir -p client/build
	cd client/build && cmake && make

server:
	-mkdir -p server/build
	cd client/build && cmake && make

test: client server
	cd server/build && ./server
	cd client/build && ./client
