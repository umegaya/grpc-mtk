PROTO_ROOT=./src/proto
PROTO_SRC_PATH=$(PROTO_ROOT)/src
GRPC_CPP_PLUGIN=grpc_cpp_plugin
GRPC_BIN_PATH=/usr/local/bin/grpc_cpp_plugin
DOCKER_IMAGE=umegaya/mgobuilder
# project root from build directory
PROJECT_ROOT=../..
BUILD_SETTING_PATH=$(PROJECT_ROOT)/tools/cmake

define call_protoc
docker run --rm -v `pwd`:/mtk $(DOCKER_IMAGE) bash -c "cd /mtk && protoc -I$(PROTO_ROOT) $1"
endef

$(PROTO_SRC_PATH)/%.pb.cc $(PROTO_SRC_PATH)/%.pb.h: $(PROTO_ROOT)/%.proto
	$(call call_protoc,--cpp_out=$(PROTO_SRC_PATH) $<)
$(PROTO_SRC_PATH)/%.grpc.pb.cc $(PROTO_SRC_PATH)/%.grpc.pb.h: $(PROTO_ROOT)/%.proto
	$(call call_protoc,--grpc_out=$(PROTO_SRC_PATH) --plugin=protoc-gen-grpc=$(GRPC_BIN_PATH) $<)

proto: $(PROTO_SRC_PATH)/mtk.pb.cc $(PROTO_SRC_PATH)/mtk.pb.h $(PROTO_SRC_PATH)/mtk.grpc.pb.cc $(PROTO_SRC_PATH)/mtk.grpc.pb.h


bundle: proto
	- mkdir -p build/osx
	cd build/osx && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/bundle.cmake $(PROJECT_ROOT) && make

ios: proto
	- mkdir -p build/ios.v7
	- mkdir -p build/ios.64
	- mkdir -p build/ios
	cd build/ios.v7 && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/ios.cmake -DIOS_ARCH=armv7 $(PROJECT_ROOT) && make
	cd build/ios.64 && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/ios.cmake -DIOS_ARCH=arm64 $(PROJECT_ROOT) && make
	lipo build/ios.v7/libmtk.a build/ios.64/libmtk.a -create -output build/ios/libmtk.a
	strip -S build/ios/libmtk.a

android: proto
	- mkdir -p build/android.v7
	- mkdir -p build/android.64
	- mkdir -p build/android
	cd build/android.v7 && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/android.cmake -DANDROID_ABI="armeabi-v7a" -DANDROID_NATIVE_API_LEVEL=android-21 -DANDROID_STL=c++_static $(PROJECT_ROOT) && make
	cd build/android.64 && cmake -DCMAKE_TOOLCHAIN_FILE=$(BUILD_SETTING_PATH)/android.cmake -DANDROID_ABI="arm64-v8a" -DANDROID_NATIVE_API_LEVEL=android-21 -DANDROID_STL=c++_static $(PROJECT_ROOT) && make
	mv build/android.v7/libmtk.so build/android/libmtk-armv7.so
	mv build/android.64/libmtk.so build/android/libmtk-arm64.so


.PHONY: build
build: bundle ios android

clean: 
	rm -r build

