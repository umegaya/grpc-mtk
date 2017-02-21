cmake_minimum_required(VERSION 3.0)
set(DEBUG false CACHE BOOL "do debug build")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -DHAVE_PTHREAD")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -DOPENSSL_NO_ASM")
include_directories(src src/proto/src grpc grpc/third_party/zlib)
include_directories(SYSTEM src/ext grpc/include grpc/third_party/boringssl/include grpc/third_party/protobuf/src)
if (DEBUG)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -DDEBUG")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2")
endif()	
file(GLOB_RECURSE libsrc [
	# lib source
	"src/*.cpp" 
	"src/*.cc" 
	# dependency source
	# "grpc/src/core/*.c"
	"grpc/src/cpp/*.cc"
	"grpc/src/boringssl/*.c"
	"grpc/third_party/boringssl/ssl/*.c"
	"grpc/third_party/boringssl/crypto/*.c"
	"grpc/third_party/protobuf/src/google/*.cc"
	# "grpc/third_party/zlib/*.c"
])
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/filelist/lists.cmake)
set(src ${libsrc} ${grpc_core_src})
file(GLOB_RECURSE rmsrc [
	# grpc files
	"grpc/src/core/lib/support/log_android.c"
	"grpc/src/cpp/ext/*.cc"
	"grpc/src/cpp/*cronet*"
	"grpc/src/cpp/common/insecure_create_auth_context.cc"
	# protobuf test files
	"grpc/third_party/protobuf/src/google/*test.cc"
	"grpc/third_party/protobuf/src/google/*test_util.cc"
	"grpc/third_party/protobuf/src/google/protobuf/testing/*.cc"
	"grpc/third_party/protobuf/src/google/protobuf/compiler/*.cc"
	"grpc/third_party/protobuf/src/google/protobuf/arena_nc.cc"
	"grpc/third_party/protobuf/src/google/protobuf/test_util_lite.cc"
	# boring ssl test files
	"grpc/third_party/boringssl/ssl/*_test.c"
	"grpc/third_party/boringssl/crypto/*_test.c"
	"grpc/third_party/boringssl/crypto/ec/example_mul.c"
	# zlib plugin and test files
	# "grpc/third_party/zlib/contrib/*.c"
	# "grpc/third_party/zlib/test/*.c"
	# "grpc/third_party/zlib/examples/*.c"
])
# message(STATUS, ${rmsrc})
list(REMOVE_ITEM src ${rmsrc})
if (IOS)
	# workaround for problem detailed in https://karp.id.au/a/2015/09/15/xcode-7s-new-linker-rules/
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -miphoneos-version-min=7.0 -Wno-nullability-completeness")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -miphoneos-version-min=7.0 -Wno-nullability-completeness")
endif()
if (OSXBUNDLE)
	set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -bundle")
	add_executable(mtk MACOSX_BUNDLE ${src}) 	
	target_link_libraries(mtk "-lz")
elseif(BUILD_ANDROID)
	add_library(mtk SHARED ${src})
	target_link_libraries(mtk "-lc++_static -lz -llog")
else()
	add_library(mtk STATIC ${src})
	target_link_libraries(mtk "-lz")
endif()
