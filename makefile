PROTO_ROOT=./src/proto
PROTO_SRC_PATH=$(PROTO_ROOT)/src
GRPC_CPP_PLUGIN=grpc_cpp_plugin
GRPC_BIN_PATH=/usr/local/bin/grpc_cpp_plugin
DOCKER_IMAGE=umegaya/mgobuilder

define call_protoc
docker run --rm -v `pwd`:/mtk $(DOCKER_IMAGE) bash -c "cd /mtk && protoc -I$(PROTO_ROOT) $1"
endef

$(PROTO_SRC_PATH)/%.pb.cc $(PROTO_SRC_PATH)/%.pb.h: $(PROTO_ROOT)/%.proto
	$(call call_protoc,--cpp_out=$(PROTO_SRC_PATH) $<)
$(PROTO_SRC_PATH)/%.grpc.pb.cc $(PROTO_SRC_PATH)/%.grpc.pb.h: $(PROTO_ROOT)/%.proto
	$(call call_protoc,--grpc_out=$(PROTO_SRC_PATH) --plugin=protoc-gen-grpc=$(GRPC_BIN_PATH) $<)

proto: $(PROTO_SRC_PATH)/mtk.pb.cc $(PROTO_SRC_PATH)/mtk.pb.h $(PROTO_SRC_PATH)/mtk.grpc.pb.cc $(PROTO_SRC_PATH)/mtk.grpc.pb.h
