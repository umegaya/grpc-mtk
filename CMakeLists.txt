cmake_minimum_required(VERSION 3.0)

# global compile options
set(DEBUG false CACHE BOOL "do debug build")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -DHAVE_PTHREAD -D_GNU_SOURCE -DWIN32_LEAN_AND_MEAN -D_HAS_EXCEPTIONS=0 -DNOMINMAX")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -DOPENSSL_NO_ASM -DOSATOMIC_USE_INLINED=1 -DPB_NO_PACKED_STRUCTS")
include_directories(src src/proto/src ext/grpc ext/grpc/third_party/zlib)
include_directories(SYSTEM 
	ext 
	ext/spdlog/include 
	ext/grpc/include 
	ext/grpc/third_party/boringssl/include 
	ext/grpc/third_party/protobuf/src 
	ext/grpc/third_party/cares 
	ext/grpc/third_party/cares/cares)
if (DEBUG)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -DDEBUG")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall -DDEBUG")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2 -DNDEBUG")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -DNDEBUG")
endif()	

# generate main file list
file(GLOB_RECURSE libsrc [
	# lib source
	"src/*.cpp" 
	"src/*.cc" 
	# dependency source
	"ext/grpc/src/core/ext/transport/cronet/*.c"
	"ext/grpc/src/core/ext/transport/inproc/*.c"
	"ext/grpc/src/core/lib/http/*.c"
	"ext/grpc/third_party/protobuf/src/google/*.cc"
])

# import file lists extracted from grpc Makefile
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/filelist/list.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/filelist/zlist.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/filelist/ssllist.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/filelist/areslist.cmake)
# add file group specific options here
set_property(SOURCE ${grpc_src_areslist} PROPERTY COMPILE_FLAGS -DHAVE_CONFIG_H)

# generate entire file list
set(src ${libsrc} ${grpc_src_list} ${grpc_src_zlist} ${grpc_src_ssllist} ${grpc_src_areslist})
file(GLOB_RECURSE rmsrc [
	# grpc files
	"ext/grpc/src/core/lib/support/log_android.c"
	"ext/grpc/src/cpp/ext/*.cc"
	"ext/grpc/src/cpp/*cronet*"
	"ext/grpc/src/cpp/common/insecure_create_auth_context.cc"
	# protobuf test files
	"ext/grpc/third_party/protobuf/src/google/*test.cc"
	"ext/grpc/third_party/protobuf/src/google/*_test*.cc"
	"ext/grpc/third_party/protobuf/src/google/*unittest*.cc"
	"ext/grpc/third_party/protobuf/src/google/*test_util*.cc"
	"ext/grpc/third_party/protobuf/src/google/protobuf/util/internal/testdata/*"
	"ext/grpc/third_party/protobuf/src/google/protobuf/*json_format_proto3*"
	"ext/grpc/third_party/protobuf/src/google/protobuf/testing/*.cc"
	"ext/grpc/third_party/protobuf/src/google/protobuf/compiler/*.cc"
	"ext/grpc/third_party/protobuf/src/google/protobuf/arena_nc.cc"
	"ext/grpc/third_party/protobuf/src/google/protobuf/test_util_lite.cc"
])
#message(STATUS, ${rmsrc})

# target settings
list(REMOVE_ITEM src ${rmsrc})
if (IOS)
	include_directories(ext/grpc/third_party/cares/config_darwin)
	# workaround for problem detailed in https://karp.id.au/a/2015/09/15/xcode-7s-new-linker-rules/
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -miphoneos-version-min=7.0 -Wno-nullability-completeness")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -miphoneos-version-min=7.0 -Wno-nullability-completeness")
endif()
if (OSXBUNDLE)
	include_directories(ext/grpc/third_party/cares/config_darwin)
	set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -bundle")
	add_executable(mtk MACOSX_BUNDLE ${src}) 	
	target_link_libraries(mtk "-lz")
elseif(BUILD_ANDROID)
	include_directories(ext/grpc/third_party/cares/config_linux)
	add_library(mtk SHARED ${src})
	target_link_libraries(mtk "-lc++_static -lz -llog")
else()
	include_directories(ext/grpc/third_party/cares/config_linux)
	add_library(mtk STATIC ${src})
	target_link_libraries(mtk "-lz")
endif()
