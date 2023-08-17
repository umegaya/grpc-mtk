cmake_minimum_required(VERSION 3.0)
project(mtk)
set(DEBUG false CACHE BOOL "do debug build")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -DHAVE_PTHREAD -DHAVE_CONFIG_H")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -DOPENSSL_NO_ASM -DPB_NO_PACKED_STRUCTS -DHAVE_CONFIG_H")
include_directories(src src/proto/src
	ext/grpc ext/grpc/src/core/ext/upb-generated ext/grpc/src/core/ext/upbdefs-generated
	ext/grpc/third_party/zlib ext/grpc/third_party/abseil-cpp ext/grpc/third_party/upb 
	ext/grpc/third_party/xxhash ext/grpc/third_party/re2 ext/grpc/third_party/cares
	ext/grpc/third_party/cares/cares/include
	ext/grpc/third_party/utf8_range
)
include_directories(SYSTEM ext 
	ext/spdlog/include ext/grpc/include 
	ext/grpc/third_party/boringssl-with-bazel/src/include 
	ext/grpc/third_party/protobuf/src
	ext/grpc/third_party/address_sorting/include
	ext/grpc/third_party/cares/cares/include
)
if (DEBUG)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -Wextra -DDEBUG")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall -Wextra -DDEBUG")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2")
endif()	
# pthread_rwlock_t requires a feature flag.
file(GLOB_RECURSE libsrc [
	# lib source
	"src/*.cpp" 
	"src/*.cc" 
	# dependency source
	"ext/grpc/src/cpp/*.cc"
	"ext/grpc/third_party/protobuf/src/google/*.cc"
	"ext/grpc/third_party/abseil-cpp/absl/log/*.cc"
	"ext/grpc/third_party/abseil-cpp/absl/debugging/*.cc"
	"ext/grpc/third_party/utf8_range/*.cc"
])
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/filelist/lists.cmake)
set(src ${libsrc} ${grpc_core_src})
file(GLOB_RECURSE ignores [
	# grpc files
	"ext/grpc/src/core/lib/support/log_android.c"
	"ext/grpc/src/cpp/ext/*.cc"
	"ext/grpc/src/cpp/*cronet*"
	"ext/grpc/src/cpp/common/insecure_create_auth_context.cc"
	"ext/grpc/src/cpp/authorization_policy_provider.cc"
	"ext/grpc/src/cpp/server/admin/*.cc"
	"ext/grpc/src/cpp/server/channelz/*.cc"
	"ext/grpc/src/cpp/server/csds/*.cc"
	"ext/grpc/src/cpp/server/load_reporter/*.cc"
	"ext/grpc/src/cpp/server/orca/*.cc"
	# protobuf unsed files
	"ext/grpc/third_party/protobuf/src/google/lazy_field_heavy.cc"
	"ext/grpc/third_party/protobuf/src/google/protobuf/compiler/*.cc"
	"ext/grpc/third_party/protobuf/src/google/protobuf/arena_nc.cc"
	"ext/grpc/third_party/protobuf/src/google/protobuf/util/json_format_proto3.pb.cc"
	# protobuf test files
	"ext/grpc/third_party/protobuf/src/google/*test*.cc"
	"ext/grpc/third_party/protobuf/src/google/protobuf/util/internal/testdata/*"
	"ext/grpc/third_party/protobuf/src/google/protobuf/testing/*.cc"
	"ext/grpc/third_party/protobuf/src/google/protobuf/test_util_lite.cc"
	# abseil test files
	"ext/grpc/third_party/abseil-cpp/absl/*test*.cc"
	"ext/grpc/third_party/abseil-cpp/absl/*mock*.cc"
	"ext/grpc/third_party/abseil-cpp/absl/*benchmark*.cc"
	# utf8_range test files
	"ext/grpc/third_party/utf8_range/*test*.cc"
])
list(REMOVE_ITEM src ${ignores})
if (MTK_BUILD STREQUAL "OSX")
	include_directories(ext/grpc/third_party/cares/config_darwin)
	find_library(CF CoreFoundation)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -bundle")
	add_executable(mtk MACOSX_BUNDLE ${src})
	target_link_libraries(mtk objc ${CF})
	target_link_libraries(mtk "-lz")
elseif (MTK_BUILD STREQUAL "Android")
	include_directories(ext/grpc/third_party/cares/config_android)
	add_library(mtk SHARED ${src})
	target_link_libraries(mtk "-lc++_static -lz -llog")
else()
	if (MTK_BUILD STREQUAL "iOS")
		include_directories(ext/grpc/third_party/cares/config_darwin)
	endif()
	add_library(mtk STATIC ${src})
	target_link_libraries(mtk "-lz")
endif()
