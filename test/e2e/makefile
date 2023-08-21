PROTO_PATH=./proto
PROTO_FILE=$(shell ls $(PROTO_PATH)/*.proto)
PROTO_SRC=$(PROTO_FILE:%.proto=%.pb.cc)
TOOLIMAGE=mtktool/builder

define call_protoc
docker run --rm -v `pwd`:/mtk $(TOOLIMAGE) bash -c "cd /mtk && protoc -I$(PROTO_PATH) $1"
endef

$(PROTO_PATH)/%.pb.cc $(PROTO_PATH)/%.pb.h: $(PROTO_PATH)/%.proto
	$(call call_protoc,--cpp_out=$(PROTO_PATH) $<)

.PHONY: proto
proto: $(PROTO_SRC)

.PHONY: client
client: testlib proto
	-mkdir -p ./client/build
	cd client/build && (rm client || true) && cmake .. && make

.PHONY: server
server: testlib proto
	-mkdir -p ./server/build
	cd server/build && (rm server || true) && cmake .. && make

test: server client
	ulimit -n 2048 && cd ./server/build && ./server &
	ulimit -n 2048 && cd ./client/build && ./client
	killall server

testlib:
	[ ! -z "${SKIPLIB}" ] || make -C $(CURDIR)/../.. testlib

clean:
	rm -rf ./client/build
	rm -rf ./server/build
