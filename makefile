PROJECT_ROOT=$(CURDIR)
PROTO_ROOT=$(PROJECT_ROOT)/src/proto
PROTO_SRC_PATH=$(PROTO_ROOT)/src
GRPC_ROOT=$(PROJECT_ROOT)/ext/grpc
GRPC_PROTO_ROOT=$(GRPC_ROOT)/src/proto
GRPC_CPP_PLUGIN=grpc_cpp_plugin
GRPC_BIN_PATH=/usr/local/bin/grpc_cpp_plugin
DOCKER_IMAGE=mtktool/builder
BUILD_SETTING_PATH=$(PROJECT_ROOT)/tools/cmake
FILELIST_TOOL_PATH=$(PROJECT_ROOT)/tools/filelist

define call_protoc
docker run --rm -v `pwd`:/mtk $(DOCKER_IMAGE) bash -c "cd /mtk && protoc -I$(PROTO_ROOT) $1"
endef

$(PROTO_SRC_PATH)/%.pb.cc $(PROTO_SRC_PATH)/%.pb.h: $(PROTO_ROOT)/%.proto
	$(call call_protoc,--cpp_out=$(PROTO_SRC_PATH) $<)
$(PROTO_SRC_PATH)/%.grpc.pb.cc $(PROTO_SRC_PATH)/%.grpc.pb.h: $(PROTO_ROOT)/%.proto
	$(call call_protoc,--grpc_out=$(PROTO_SRC_PATH) --plugin=protoc-gen-grpc=$(GRPC_BIN_PATH) $<)
$(FILELIST_TOOL_PATH)/lists.cmake: $(GRPC_ROOT)/Makefile
	make -C $(FILELIST_TOOL_PATH) list 2>/dev/null | bash $(FILELIST_TOOL_PATH)/gen.sh $@

proto: $(PROTO_SRC_PATH)/mtk.pb.cc $(PROTO_SRC_PATH)/mtk.pb.h $(PROTO_SRC_PATH)/mtk.grpc.pb.cc $(PROTO_SRC_PATH)/mtk.grpc.pb.h

filelist: $(FILELIST_TOOL_PATH)/lists.cmake

toolchains: 
	bash $(BUILD_SETTING_PATH)/resources/gen.sh

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
	- mkdir -p build/ios
	cd build/ios && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/ios.cmake -G Xcode $(PROJECT_ROOT) && \
		cmake --build . --config Release && cmake --install . --config Release && cp Release-iphoneos/libmtk.a libmtk.a
	strip -S build/ios/libmtk.a

android: proto filelist
	- mkdir -p build/android
	cd build/android && cmake -DCMAKE_TOOLCHAIN_FILE=${BUILD_SETTING_PATH}/android.cmake \
		-DMTK_ANDROID_NDK=${ANDROID_NDK_HOME} $(PROJECT_ROOT) && make

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
	docker build -t $(DOCKER_IMAGE) .
