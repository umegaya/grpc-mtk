cmake_minimum_required(VERSION 3.0)
set(DEBUG true CACHE BOOL "do debug build")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -DHAVE_PTHREAD")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -DOPENSSL_NO_ASM")
include_directories(. ..)
include_directories(SYSTEM ../../../src ../../../ext ../../../ext/grpc/third_party/protobuf/src
					../../../ext/spdlog/include)
if (DEBUG)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -DDEBUG")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2")
endif()	

file(GLOB_RECURSE src [
	"./main.cpp" 
	"../proto/*.cc"
])

link_directories(../../../build/test)
add_executable(server ${src}) 	
target_link_libraries(server mtk z)
