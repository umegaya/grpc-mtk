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
client: proto
	-mkdir -p ./client/build
	cd client/build && cmake .. && make

.PHONY: server
server: proto
	-mkdir -p ./server/build
	cd server/build && cmake .. && make

test: client server
	cd ./server/build && ./server &
	cd ./client/build && ./client

clean:
	rm -rf ./client/build
	rm -rf ./server/build
