PROTO_ROOT=./src/proto
COMMON_PROTO_ROOT=./ext/grpc/third_party/protobuf/src/google/protobuf
PROTO_SRC_PATH=$(PROTO_ROOT)/src
GRPC_ROOT=./ext/grpc
GRPC_PROTO_ROOT=$(GRPC_ROOT)/src/proto
GRPC_CPP_PLUGIN=grpc_cpp_plugin
GRPC_BIN_PATH=/usr/local/bin/grpc_cpp_plugin
DOCKER_IMAGE=mtktool/builder
GRPC_COMMIT=$(shell bash ./tools/builder/hash.sh)
GRPC_PROTO_SRCS=$(shell find $(GRPC_PROTO_ROOT)/grpc/[^t]* -name *.proto)
# project root from build directory
PROJECT_ROOT=../..
BUILD_SETTING_PATH=$(PROJECT_ROOT)/tools/cmake
FILELIST_TOOL_PATH=./tools/filelist

define call_protoc
docker run --rm -v `pwd`:/mtk $(DOCKER_IMAGE) bash -c "cd /mtk && protoc -I$(PROTO_ROOT) -I$(COMMON_PROTO_ROOT) $1"
endef

$(PROTO_SRC_PATH)/%.pb.cc $(PROTO_SRC_PATH)/%.pb.h: $(PROTO_ROOT)/%.proto
	$(call call_protoc,--cpp_out=$(PROTO_SRC_PATH) $<)
$(PROTO_SRC_PATH)/%.grpc.pb.cc $(PROTO_SRC_PATH)/%.grpc.pb.h: $(PROTO_ROOT)/%.proto
	$(call call_protoc,--grpc_out=$(PROTO_SRC_PATH) --plugin=protoc-gen-grpc=$(GRPC_BIN_PATH) $<)
$(GRPC_PROTO_ROOT)/grpc/%.pb.cc $(GRPC_PROTO_ROOT)/grpc/%.pb.h: $(GRPC_PROTO_ROOT)/grpc/%.proto 
	$(call call_protoc,-I. --cpp_out=. $<)
$(FILELIST_TOOL_PATH)/%.cmake: $(GRPC_ROOT)/Makefile $(FILELIST_TOOL_PATH)/makefile
	make -C $(FILELIST_TOOL_PATH) $(basename $(notdir $@)) 2>/dev/null | bash $(FILELIST_TOOL_PATH)/gen.sh grpc_src_$(basename $(notdir $@)) $@

proto: grpc_proto $(PROTO_SRC_PATH)/mtk.pb.cc $(PROTO_SRC_PATH)/mtk.pb.h $(PROTO_SRC_PATH)/mtk.grpc.pb.cc $(PROTO_SRC_PATH)/mtk.grpc.pb.h
grpc_proto: $(GRPC_PROTO_SRCS:.proto=.pb.cc)

filelist: $(FILELIST_TOOL_PATH)/list.cmake $(FILELIST_TOOL_PATH)/zlist.cmake $(FILELIST_TOOL_PATH)/ssllist.cmake $(FILELIST_TOOL_PATH)/areslist.cmake 

linux: proto filelist
	- mkdir -p build/linux
	cd build/linux && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/linux.cmake $(PROJECT_ROOT) && make

bundle: proto filelist
	- mkdir -p build/osx
	cd build/osx && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/bundle.cmake $(PROJECT_ROOT) && make

testlib: proto filelist
	- mkdir -p build/test
	cd build/test && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/testlib.cmake $(PROJECT_ROOT) && make

ios: proto filelist
	- mkdir -p build/ios.v7
	- mkdir -p build/ios.64
	- mkdir -p build/ios
	cd build/ios.v7 && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/ios.cmake -DIOS_ARCH=armv7 $(PROJECT_ROOT) && make
	cd build/ios.64 && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/ios.cmake -DIOS_ARCH=arm64 $(PROJECT_ROOT) && make
	lipo build/ios.v7/libmtk.a build/ios.64/libmtk.a -create -output build/ios/libmtk.a
	strip -S build/ios/libmtk.a

android: proto filelist
	- mkdir -p build/android.v7
	- mkdir -p build/android.64
	- mkdir -p build/android
	cd build/android.v7 && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/android.cmake -DANDROID_ABI="armeabi-v7a" -DANDROID_NATIVE_API_LEVEL=android-21 -DANDROID_STL=c++_static $(PROJECT_ROOT) && make
	cd build/android.64 && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/android.cmake -DANDROID_ABI="arm64-v8a" -DANDROID_NATIVE_API_LEVEL=android-21 -DANDROID_STL=c++_static $(PROJECT_ROOT) && make
	mv build/android.v7/libmtk.so build/android/libmtk-armv7.so
	mv build/android.64/libmtk.so build/android/libmtk-arm64.so

install:
	mkdir -p /usr/local/lib && cp build/linux/libmtk.a /usr/local/lib
	mkdir -p /usr/local/include/mtk && find ./src -name '*.h' -exec cp -prv '{}' '/usr/local/include/mtk/' ';'

.PHONY: build
build: bundle ios android

.PHONY: test
test: e2etest httptest

e2etest: testlib
	make -C test/e2e server client

httptest: testlib
	make -C test/http server client

clean: 
	rm -r build

builder:
	docker build --build-arg GRPC_COMMIT=$(GRPC_COMMIT) -t mtktool/builder .



